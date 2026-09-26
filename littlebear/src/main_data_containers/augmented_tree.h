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

inline value_pointer vpointer_create(uint64_t offset, uint16_t size) {return ((offset & six_byte_mask) | (uint64_t)size << 48);}
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
  return (a > b) - (a < b);
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
  // An empty tree needs no internal node or rank allocation.
  if (is_empty(tree)) {
    const uint32_t leaf = geometric_rank_partioned_virtual_slab_arena::reserve(tree->node_allocator, EXTERNAL_NODE_PARTITION);
    if (leaf == INVALID_NODE_INDEX) return OUT_OF_MEMORY;
    node_set_external(tree, node_get_data(tree, leaf), leaf, k, value_hash, v);
    tree->root = leaf;
    return NO_ERRORS;
  }
  //allocate new nodes
  uint_fast8_t rank = std::min(std::countl_zero(value_hash), NUMBER_OF_RANKS - 1);
  uint32_t new_internal_node_index = geometric_rank_partioned_virtual_slab_arena::reserve(tree->node_allocator, rank + 1);
  if (new_internal_node_index == INVALID_NODE_INDEX) return OUT_OF_MEMORY;
  uint32_t new_external_node_index = geometric_rank_partioned_virtual_slab_arena::reserve(tree->node_allocator, EXTERNAL_NODE_PARTITION);
  if (new_external_node_index == INVALID_NODE_INDEX) {
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, new_internal_node_index);
    return OUT_OF_MEMORY;
  }
  node_data new_internal_node = node_get_data(tree, new_internal_node_index);
  node_data new_external_node = node_get_data(tree, new_external_node_index);
  node_set_external(tree, new_external_node, new_external_node_index, k, value_hash, v);

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

  // Find the split fingerprint before modifying the path. Each unzip spine
  // then gets its final summary directly, without a stack or ancestor repair.
  uint64_t left_hash = XOR_IDENTITY;
  uint32_t leaf_index = current_index;
  while (node_rank(leaf_index) != -1) {
    node_data n = node_get_data(tree, leaf_index);
    if (k < n[node_item::pathmarker].value) leaf_index = n[node_item::children].left_index;
    else {
      left_hash ^= tree->hash_accumulator_array[n[node_item::children].left_index];
      leaf_index = n[node_item::children].right_index;
    }
  }
  node_data leaf = node_get_data(tree, leaf_index);
  node_set_partial_internal(tree, new_internal_node, new_internal_node_index, k, value_hash ^ tree->hash_accumulator_array[current_index]);
  if (k < leaf[node_item::k].value) {
    // Prepending to this subtree leaves its structure and summaries unchanged.
    new_internal_node[node_item::pathmarker].value = leaf[node_item::k].value;
    new_internal_node[node_item::children].left_index = new_external_node_index;
    new_internal_node[node_item::children].right_index = current_index;
    return NO_ERRORS;
  }
  left_hash ^= tree->hash_accumulator_array[leaf_index];
  uint64_t right_hash = tree->hash_accumulator_array[current_index] ^ left_hash ^ value_hash;
  node_data left_unzip_iterator = nullptr;
  node_data right_unzip_iterator = nullptr;
  while (node_rank(current_index) != -1) {
    if (current[node_item::pathmarker].value < k) {
      if (left_unzip_iterator == nullptr) new_internal_node[node_item::children].left_index = current_index;
      else left_unzip_iterator[node_item::children].right_index = current_index;
      tree->hash_accumulator_array[current_index] = left_hash;
      left_hash ^= tree->hash_accumulator_array[current[node_item::children].left_index];
      left_unzip_iterator = current;
      current_index = current[node_item::children].right_index;
      current = node_get_data(tree, current_index);
    } else {
      if (right_unzip_iterator == nullptr) new_internal_node[node_item::children].right_index = current_index;
      else right_unzip_iterator[node_item::children].left_index = current_index;
      tree->hash_accumulator_array[current_index] = right_hash;
      right_hash ^= tree->hash_accumulator_array[current[node_item::children].right_index];
      right_unzip_iterator = current;
      current_index = current[node_item::children].left_index;
      current = node_get_data(tree, current_index);
    }
  }

  if (left_unzip_iterator == nullptr) new_internal_node[node_item::children].left_index = current_index;
  else left_unzip_iterator[node_item::children].right_index = current_index;
  if (right_unzip_iterator == nullptr) new_internal_node[node_item::children].right_index = new_external_node_index;
  else right_unzip_iterator[node_item::children].left_index = new_external_node_index;

  return error_codes::NO_ERRORS;
}

// Unsafe: k must exist and value_hash must match its leaf fingerprint.
inline void unsafe_dissociate(header *tree, key k, XXH64_hash_t value_hash) {
  uint32_t *link = &tree->root;
  uint32_t *parent_link = nullptr;
  uint32_t parent_index = INVALID_NODE_INDEX;
  uint32_t current_index = *link;
  node_data current = node_get_data(tree, current_index);

  while (node_rank(current_index) != -1 && current[node_item::pathmarker].value != k) {
    tree->hash_accumulator_array[current_index] ^= value_hash;
    parent_link = link;
    parent_index = current_index;
    link = k < current[node_item::pathmarker].value
      ? &current[node_item::children].left_index
      : &current[node_item::children].right_index;
    current_index = *link;
    current = node_get_data(tree, current_index);
  }

  // The smallest key has no internal node with its key as a pathmarker.
  if (node_rank(current_index) == -1) {
    if (parent_link == nullptr) tree->root = INVALID_NODE_INDEX;
    else {
      *parent_link = node_get_data(tree, parent_index)[node_item::children].right_index;
      geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, parent_index);
    }
    geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, current_index);
    return;
  }

  // Zip the two spines, replacing the right subtree's minimum leaf (k) with
  // the remaining left subtree. Carry the final XOR summary down the path;
  // fixed off-path subtrees can be subtracted without a stack or extra nodes.
  uint32_t left = current[node_item::children].left_index;
  uint32_t right = current[node_item::children].right_index;
  uint64_t remaining_hash = tree->hash_accumulator_array[current_index] ^ value_hash;
  while (node_rank(right) != -1) {
    if (node_rank(left) != -1 && compare_node_ranks(left, right) > 0) {
      node_data n = node_get_data(tree, left);
      *link = left;
      tree->hash_accumulator_array[left] = remaining_hash;
      remaining_hash ^= tree->hash_accumulator_array[n[node_item::children].left_index];
      link = &n[node_item::children].right_index;
      left = *link;
    } else {
      node_data n = node_get_data(tree, right);
      *link = right;
      tree->hash_accumulator_array[right] = remaining_hash;
      remaining_hash ^= tree->hash_accumulator_array[n[node_item::children].right_index];
      link = &n[node_item::children].left_index;
      right = *link;
    }
  }
  *link = left;
  geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, right);
  geometric_rank_partioned_virtual_slab_arena::release(tree->node_allocator, current_index);
}

inline uint32_t find(header *tree, key k) {
  if (is_empty(tree)) return INVALID_NODE_INDEX;
  uint32_t current_index = tree->root;
  while (node_rank(current_index) != -1) {
    node_data current = node_get_data(tree, current_index);
    if (k < current[node_item::pathmarker].value) current_index = current[node_item::children].left_index;
    else current_index = current[node_item::children].right_index;
  }
  node_data current = node_get_data(tree, current_index);
  return current[node_item::k].value == k ? current_index : INVALID_NODE_INDEX;
}

inline value_pointer get(header *tree, key k) {
  const uint32_t index = find(tree, k);
  return index == INVALID_NODE_INDEX ? NULLVALUEPOINTER : node_get_data(tree, index)[node_item::v].value;
}

// Replace without allocating or changing the tree topology/rank priorities.
// Find first so a missing key leaves every subtree fingerprint untouched.
inline bool replace(header *tree, key k, value_pointer v, XXH64_hash_t value_hash) {
  const uint32_t leaf = find(tree, k);
  if (leaf == INVALID_NODE_INDEX) return false;
  const uint64_t delta = tree->hash_accumulator_array[leaf] ^ value_hash;
  uint32_t index = tree->root;
  while (index != leaf) {
    tree->hash_accumulator_array[index] ^= delta;
    node_data n = node_get_data(tree, index);
    index = k < n[node_item::pathmarker].value
      ? n[node_item::children].left_index : n[node_item::children].right_index;
  }
  node_set_external(tree, node_get_data(tree, leaf), leaf, k, value_hash, v);
  return true;
}

// Fingerprint of keys strictly below k within the supplied subtree.
inline XXH64_hash_t get_fingerprint(header *tree, uint32_t index, key k) {
  uint64_t hash = XOR_IDENTITY;
  if (index == INVALID_NODE_INDEX) return hash;
  while (node_rank(index) != -1) {
    node_data n = node_get_data(tree, index);
    if (k <= n[node_item::pathmarker].value) index = n[node_item::children].left_index;
    else {
      hash ^= tree->hash_accumulator_array[n[node_item::children].left_index];
      index = n[node_item::children].right_index;
    }
  }
  if (node_get_data(tree, index)[node_item::k].value < k)
    hash ^= tree->hash_accumulator_array[index];
  return hash;
}

// Exact half-open interval [low, high); UINT64_MAX is a valid key, not infinity.
inline XXH64_hash_t get_range_fingerprint(header *tree, key low_inclusive, key high_exclusive) {
  if (is_empty(tree) || low_inclusive >= high_exclusive) return XOR_IDENTITY;
  const uint64_t low = low_inclusive == 0 ? XOR_IDENTITY : get_fingerprint(tree, tree->root, low_inclusive);
  return low ^ get_fingerprint(tree, tree->root, high_exclusive);
}

inline XXH64_hash_t get_full_fingerprint(header *tree) {
  return is_empty(tree) ? XOR_IDENTITY : tree->hash_accumulator_array[tree->root];
}

}
