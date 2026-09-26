#pragma once
#include "../xxhash.h"
#include "geometric_rank_partitioned_virtual_arena.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <bit>

constexpr uint64_t five_bit_mask = 0x1F;
constexpr uint64_t five_nine_bit_mask = 0x07FFFFFFFFFFFFFF;
constexpr uint64_t six_byte_mask =  0x0000FFFFFFFFFFFF;
constexpr uint64_t four_byte_mask = 0x00000000FFFFFFFF;
constexpr uint64_t two_byte_mask =  0x000000000000FFFF;
constexpr XXH64_hash_t XOR_IDENTITY = 0;

//we use an external zip_zip tree with an accumulator entry in each node to enable efficient fingerprinting
namespace augmented_tree {

using value_pointer = uint64_t;
constexpr uint64_t NULLVALUEPOINTER = 0;

inline value_pointer vpointer_create(void *address, uint16_t size) {return (((uint64_t)address & six_byte_mask) | (uint64_t)size << 48);}
inline uint64_t vpointer_get_offset(const value_pointer p) {return p & six_byte_mask;}
inline uint16_t vpointer_get_size(const value_pointer p) {return (p >> 48) & two_byte_mask;}

constexpr uint32_t INVALID_NODE_INDEX = geometric_rank_partioned_virtual_slab_arena::INVALID_INDEX;

using key = uint64_t;
constexpr key MIN_KEY = 0;
constexpr key MAX_KEY = UINT64_MAX;

struct header {
  uint32_t root = INVALID_NODE_INDEX;
  geometric_rank_partioned_virtual_slab_arena::allocator *node_allocator;
  uint64_t *hash_accumulator_array;
};

//each node is 16 bytes,
union node_entry {
  struct {uint32_t left_index; uint32_t right_index;};
  uint64_t value;
};

//a node is an array of two node entries
using node_data = node_entry *;

//a node can either be internal or external, sometimes we don't care whether or not a node is internal or external, we just want to know its pathmarker/k
enum node_item {
  pathmarker = 0, children = 1,
  k = 0, v = 1,
  k_or_pathmarker = 0, //we don't have v_or_children because trying to access either left/right or number would be ub
};

inline node_data node_get_data(header *tree, const uint32_t node_index) {
  return (node_data)geometric_rank_partioned_virtual_slab_arena::address(tree->node_allocator, node_index);
}

inline void node_set_external(header *tree, node_data node, uint32_t node_index, key k, XXH64_hash_t value_hash, value_pointer v) {
  node[node_item::k].value = k;
  node[node_item::v].value = v;
  tree->hash_accumulator_array[node_index] = value_hash;
}

inline void node_set_partial_internal(header *tree, node_data node, uint32_t node_index, key pathmarker, XXH64_hash_t accumulator) {
  node[node_item::pathmarker].value = pathmarker;
  node[node_item::children].left_index = INVALID_NODE_INDEX;
  node[node_item::children].right_index = INVALID_NODE_INDEX;
  tree->hash_accumulator_array[node_index] = accumulator;
}

inline void node_repoint_left(header *tree, node_data node, uint32_t node_index, uint32_t new_left_index) {
  tree->hash_accumulator_array[node_index] ^= tree->hash_accumulator_array[node[node_item::children].left_index];
  tree->hash_accumulator_array[node_index] ^= tree->hash_accumulator_array[new_left_index];
  node[node_item::children].left_index = new_left_index;
}

inline void node_repoint_right(header *tree, node_data node, uint32_t node_index, uint32_t new_right_index) {
  tree->hash_accumulator_array[node_index] ^= tree->hash_accumulator_array[node[node_item::children].right_index];
  tree->hash_accumulator_array[node_index] ^= tree->hash_accumulator_array[new_right_index];
  node[node_item::children].right_index = new_right_index;
}

inline int node_rank(uint32_t node_index) {
  return geometric_rank_partioned_virtual_slab_arena::partition(node_index) - 1;
}

constexpr int NUMBER_OF_RANKS = 31;
constexpr size_t NODE_SIZE = 16;

inline header create(geometric_rank_partioned_virtual_slab_arena::allocator *node_allocator, uint64_t *hash_accumulator_array) {
  return header{.root = INVALID_NODE_INDEX, .node_allocator = node_allocator, .hash_accumulator_array = hash_accumulator_array};
}

inline bool is_empty(header *tree) {return tree->root == INVALID_NODE_INDEX;}

enum error_codes {
  NO_ERRORS,
  OUT_OF_MEMORY,
};

enum class travel_hint {
  left, //travel left or came from node.left
  right, //travel right or came from node.right
  none //at the root or don't need to travel anymore
};

inline int compare_node_ranks(uint32_t a, uint32_t b) {
  uint_fast8_t rank_a = geometric_rank_partioned_virtual_slab_arena::partition(a) - 1;
  uint_fast8_t rank_b = geometric_rank_partioned_virtual_slab_arena::partition(b) - 1;
  if (rank_a > rank_b) return 1;
  if (rank_a < rank_b) return -1;
  return a - b;
}

inline bool can_replace_node(key existing_node_key, uint32_t existing_node_index, uint32_t inserted_node_index, key inserted_node_key, uint_fast8_t inserted_rank) {
  uint_fast8_t existing_node_rank = geometric_rank_partioned_virtual_slab_arena::partition(existing_node_index) - 1;
  if (inserted_rank > existing_node_rank) return true;
  if (existing_node_rank > inserted_rank) return false;
  if (inserted_node_index > existing_node_index) return true;
  if (existing_node_index > inserted_node_index) return false;
  if (inserted_node_key >= existing_node_key) return false;
  return true;
}

constexpr uint8_t EXTERNAL_NODE_PARTITION = 0;

//unsafe because we assume that k isn't already in our tree
inline int unsafe_associate(header *tree, key k, value_pointer v, XXH64_hash_t value_hash) {
  //allocate new nodes
  uint_fast8_t rank = std::min(std::countl_zero(value_hash), NUMBER_OF_RANKS - 1);
  uint32_t new_internal_node_index = geometric_rank_partioned_virtual_slab_arena::reserve(tree->node_allocator, rank + 1);
  uint32_t new_external_node_index = geometric_rank_partioned_virtual_slab_arena::reserve(tree->node_allocator, EXTERNAL_NODE_PARTITION);
  if (new_internal_node_index == INVALID_NODE_INDEX || new_external_node_index == INVALID_NODE_INDEX) return error_codes::OUT_OF_MEMORY;
  node_data new_internal_node = node_get_data(tree, new_internal_node_index);
  node_data new_external_node = node_get_data(tree, new_external_node_index);
  node_set_external(tree, new_external_node, new_external_node_index, k, value_hash, v);

  //if tree is empty only place the external node
  if (is_empty(tree)) {
    tree->root = new_external_node_index;
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, new_internal_node_index);
    return error_codes::NO_ERRORS;
  }

  //traverse the tree till we find the insertion point for our new internal node
  uint32_t parent_index = tree->root;
  uint32_t current_index = parent_index;
  node_data current = node_get_data(tree, current_index);
  travel_hint parent_child_relationship = travel_hint::none;
  while ((node_rank(current_index) != -1) && !can_replace_node(current[node_item::k].value, current_index, new_internal_node_index, k, rank)) {
    parent_index = current_index;
    tree->hash_accumulator_array[current_index] ^= value_hash;
    if (current[node_item::pathmarker].value < k) {parent_child_relationship = travel_hint::right; current_index = current[node_item::children].right_index;}
    else {parent_child_relationship = travel_hint::left; current_index = current[node_item::children].left_index;}
    current = node_get_data(tree, current_index);
  }

  switch (parent_child_relationship) {
    case travel_hint::none:  tree->root = new_internal_node_index; break;
    case travel_hint::left:  node_get_data(tree, parent_index)[node_item::children].left_index = new_internal_node_index; break;
    case travel_hint::right: node_get_data(tree, parent_index)[node_item::children].right_index = new_internal_node_index; break;
  }

  //traverse from the found node to the end of the path, adding nodes to the left unzip and the right unzip
  node_set_partial_internal(tree, new_internal_node, new_internal_node_index, k, value_hash ^ tree->hash_accumulator_array[current_index]);
  node_data left_unzip_iterator = nullptr;
  uint32_t left_unzip_iterator_index = INVALID_NODE_INDEX;
  node_data right_unzip_iterator = nullptr;
  uint32_t right_unzip_iterator_index = INVALID_NODE_INDEX;
  while (node_rank(current_index) != -1) {
    if (current[node_item::pathmarker].value < k) {
      if (left_unzip_iterator == nullptr) new_internal_node[node_item::children].left_index = current_index;
      else node_repoint_right(tree, left_unzip_iterator, left_unzip_iterator_index, current_index);
      left_unzip_iterator = current;
      left_unzip_iterator_index = current_index;
      current_index = current[node_item::children].right_index;
      current = node_get_data(tree, current_index);
    } else {
      if (right_unzip_iterator == nullptr) new_internal_node[node_item::children].right_index = current_index;
      else node_repoint_left(tree, right_unzip_iterator, right_unzip_iterator_index, current_index);
      right_unzip_iterator = current;
      right_unzip_iterator_index = current_index;
      tree->hash_accumulator_array[right_unzip_iterator_index] ^= value_hash;
      current_index = current[node_item::children].left_index;
      current = node_get_data(tree, current_index);
    }
  }

  if (current[node_item::k].value < k) {
    if (left_unzip_iterator == nullptr) new_internal_node[node_item::children].left_index = current_index;
    else node_repoint_right(tree, left_unzip_iterator, left_unzip_iterator_index, current_index);
    if (right_unzip_iterator == nullptr) new_internal_node[node_item::children].right_index = new_external_node_index;
    else node_repoint_left(tree, right_unzip_iterator, right_unzip_iterator_index, new_external_node_index);
  } else {
    new_internal_node[node_item::children].left_index = new_external_node_index;
    new_internal_node[node_item::pathmarker].value = current[node_item::k].value;
    if (right_unzip_iterator == nullptr) new_internal_node[node_item::children].right_index = current_index;
    else node_repoint_left(tree, right_unzip_iterator, right_unzip_iterator_index, current_index);
    uint32_t right_unzip_iterator_index = new_internal_node[node_item::children].right_index;
    while (node_rank(right_unzip_iterator_index) != -1) {
      node_data node_at_iterator = node_get_data(tree, right_unzip_iterator_index);
      tree->hash_accumulator_array[right_unzip_iterator_index] ^= value_hash;
      right_unzip_iterator_index = node_at_iterator[node_item::children].left_index;
    }
  }

  return error_codes::NO_ERRORS;
}

//unsafe because we assume that k is in our tree
inline void unsafe_dissociate(header *tree, key k, XXH64_hash_t value_hash) {
  //clear tree if it only contains one item
  if (node_rank(tree->root) == -1) {
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, tree->root);
    tree->root = INVALID_NODE_INDEX;
    return;
  }

  uint32_t grand_parent_index = tree->root;
  node_data grand_parent = node_get_data(tree, tree->root);
  travel_hint grand_parent_to_parent_relationship = travel_hint::none;
  uint32_t parent_index = grand_parent_index;
  node_data parent = grand_parent;
  travel_hint parent_to_current_relationship = travel_hint::none;
  uint32_t current_index = parent_index;
  node_data current = parent;

  //search for the internal node with the key, keep track of grand_parent (travel_direction) parent (travel_direction) node_with_key
  while (current[node_item::k_or_pathmarker].value != k) {
    grand_parent_index = parent_index;
    grand_parent = parent;
    grand_parent_to_parent_relationship = parent_to_current_relationship;
    parent_index = current_index;
    parent = current;
    tree->hash_accumulator_array[current_index] ^= value_hash;
    if (k < current[node_item::k_or_pathmarker].value) {
      parent_to_current_relationship = travel_hint::left;
      current = node_get_data(tree, current[node_item::children].left_index);
    } else {
      parent_to_current_relationship = travel_hint::right;
      current = node_get_data(tree, current[node_item::children].right_index);
    }
  }

  //if the found node is actually external (means we're deleting the smallest key in the tree), delete it and its parent, replace the deleted parent with its right child, end
  if (node_rank(current_index) == -1) {
    if (grand_parent_to_parent_relationship == travel_hint::none) tree->root = parent[node_item::children].right_index;
    else                                                          node_repoint_left(tree, grand_parent, grand_parent_index, parent[node_item::children].right_index);
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, parent_index);
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, current_index);
    return;
  }

  //zip together left and right spines
  enum spine {
    right_pointing,
    left_pointing,
  };

  uint32_t top_of_zip;
  spine top_of_zip_spine;
  if (node_rank(current[node_item::children].right_index) == -1) { top_of_zip = current[node_item::children].left_index; top_of_zip_spine = right_pointing;}
  else if (node_rank(current[node_item::children].left_index) == -1) { top_of_zip = current[node_item::children].right_index; top_of_zip_spine = left_pointing;}
  else if (compare_node_ranks(current[node_item::children].left_index, current[node_item::children].right_index) >= 0) { top_of_zip = current[node_item::children].left_index; top_of_zip_spine = right_pointing;}
  else { top_of_zip = current[node_item::children].right_index; top_of_zip_spine = left_pointing;}

  auto has_greater_rank = [](uint32_t a, uint32_t b) {return (((node_rank(b) != -1) && compare_node_ranks(a,b) >= 0) || (node_rank(b) == -1));};

  uint32_t zip_path_iterator = (node_rank(current[node_item::children].right_index) == -1) ? current[node_item::children].right_index : top_of_zip;
  spine iterator_spine = top_of_zip_spine;
  uint32_t right_pointing_spine_iterator = current[node_item::children].left_index;
  uint32_t left_pointing_spine_iterator = current[node_item::children].right_index;

  while (node_get_data(tree, zip_path_iterator)[node_item::k].value != k) {
    switch (iterator_spine) {
      case right_pointing:
        if ((node_rank(zip_path_iterator) != -1) && has_greater_rank(zip_path_iterator, left_pointing_spine_iterator)) {
          do {
            right_pointing_spine_iterator = zip_path_iterator;
            zip_path_iterator = node_get_data(tree, zip_path_iterator)[node_item::children].right_index;
          } while ((node_rank(zip_path_iterator) != -1) && has_greater_rank(zip_path_iterator, left_pointing_spine_iterator));
          node_repoint_right(tree, node_get_data(tree, right_pointing_spine_iterator), right_pointing_spine_iterator, left_pointing_spine_iterator);
          right_pointing_spine_iterator = zip_path_iterator;
        }
        zip_path_iterator = left_pointing_spine_iterator;
        iterator_spine = left_pointing;
        break;
      case left_pointing:
        if ((node_rank(zip_path_iterator) != -1) && has_greater_rank(zip_path_iterator, right_pointing_spine_iterator)) {
          do {
            left_pointing_spine_iterator = zip_path_iterator;
            tree->hash_accumulator_array[zip_path_iterator] ^= value_hash;
            zip_path_iterator = node_get_data(tree, zip_path_iterator)[node_item::children].left_index;
          } while ((node_rank(zip_path_iterator) != -1) && has_greater_rank(zip_path_iterator, right_pointing_spine_iterator));
          node_repoint_left(tree, node_get_data(tree, left_pointing_spine_iterator), left_pointing_spine_iterator, right_pointing_spine_iterator);
          left_pointing_spine_iterator = zip_path_iterator;
        }
        zip_path_iterator = right_pointing_spine_iterator;
        iterator_spine = right_pointing;
        break;
    }
  }

  //replace the found internal node with the top of the zip path
  switch (parent_to_current_relationship) {
    case travel_hint::left:  node_repoint_left(tree, parent, parent_index, top_of_zip); break;
    case travel_hint::right: node_repoint_right(tree, parent, parent_index, top_of_zip); break;
    case travel_hint::none:  tree->root = top_of_zip; break;
  }

  //delete the external node containing our key
  geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, zip_path_iterator);

  //delete the found internal node
  geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, current_index);
}

inline value_pointer get(header *tree, key k) {
  if (is_empty(tree)) return NULLVALUEPOINTER;
  uint32_t current_index = tree->root;
  while (node_rank(current_index) != -1) {
    node_data current = node_get_data(tree, current_index);
    if (k < current[node_item::pathmarker].value) current_index = current[node_item::children].left_index;
    else current_index = current[node_item::children].right_index;
  }
  node_data current = node_get_data(tree, current_index);
  if (current[node_item::k].value == k) return current[node_item::v].value;
  else return NULLVALUEPOINTER;
}

// get fingerprint [NEGATIVE_INFINITY k)
inline XXH64_hash_t get_fingerprint(header *tree, uint32_t n_index, key k, XXH64_hash_t acc_so_far = XOR_IDENTITY, travel_hint last_direction = travel_hint::none) {
  node_data n = node_get_data(tree, n_index);
  bool is_external_node = (node_rank(n_index) == -1);
  travel_hint travel_to;
  XXH64_hash_t propagating_hash = acc_so_far;
  if (k < n[node_item::k_or_pathmarker].value) {
    switch (last_direction) {
      case travel_hint::left:        break;
      case travel_hint::right:       propagating_hash ^= tree->hash_accumulator_array[n_index]; break;
      case travel_hint::none:        propagating_hash = XOR_IDENTITY; break;}
    travel_to = travel_hint::left;

  } else if (k > n[node_item::k_or_pathmarker].value) {
    switch (last_direction) {
      case travel_hint::left:        propagating_hash ^= tree->hash_accumulator_array[n_index]; break;
      case travel_hint::right:       break;
      case travel_hint::none:        propagating_hash = tree->hash_accumulator_array[n_index]; break;}
    travel_to = travel_hint::right;

  } else {
    switch (last_direction) {
      case travel_hint::left:        propagating_hash ^= (is_external_node ? XOR_IDENTITY : tree->hash_accumulator_array[n[node_item::children].left_index]); break;
      case travel_hint::right:       propagating_hash ^= (is_external_node ? XOR_IDENTITY : tree->hash_accumulator_array[n_index] ^ tree->hash_accumulator_array[n[node_item::children].left_index]); break;
      case travel_hint::none:        propagating_hash  = (is_external_node ? XOR_IDENTITY : tree->hash_accumulator_array[n[node_item::children].left_index]); break;}
    travel_to = travel_hint::none;
  }

  if (is_external_node) return propagating_hash;
  else switch (travel_to) {case travel_hint::left:  return get_fingerprint(tree, n[node_item::children].left_index, k, propagating_hash, travel_to);
                           case travel_hint::right: return get_fingerprint(tree, n[node_item::children].right_index, k, propagating_hash, travel_to);
                           case travel_hint::none:  return propagating_hash;};
}

inline XXH64_hash_t get_range_fingerprint(header *tree, key low_inclusive, key high_exclusive) {
  if (is_empty(tree)) return XOR_IDENTITY;

  XXH64_hash_t start_to_low = XOR_IDENTITY;
  if (low_inclusive != MIN_KEY) start_to_low = get_fingerprint(tree, tree->root, low_inclusive);

  XXH64_hash_t start_to_high = XOR_IDENTITY;
  if (high_exclusive != MAX_KEY) start_to_high = get_fingerprint(tree, tree->root, high_exclusive);
  else start_to_high = tree->hash_accumulator_array[tree->root];

  return start_to_high ^ start_to_low;
}

}
