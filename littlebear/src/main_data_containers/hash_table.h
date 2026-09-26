#pragma once
#include "../xxhash.h"
#include "augmented_tree.h"
#include <cstdlib>

namespace hash_table {

using namespace_identifier = XXH128_hash_t;
inline bool operator== (namespace_identifier a, namespace_identifier b) {return a.high64 == b.high64 && a.low64 == b.low64;}
using key = namespace_identifier;
using value = zip_zip::header;

inline bool value_is_null(value *v) {
  return zip_zip::is_empty(v);
}

constexpr key EMPTY = key{XXH128_hash_t{UINT64_MAX, UINT64_MAX}};
constexpr size_t BUCKET_SIZE = 8;
constexpr size_t DEFAULT_BUCKET_COUNT = 128;

struct alignas(64) bucket {key keys[BUCKET_SIZE]; value values[BUCKET_SIZE];};

struct two_way {bucket* data; size_t bucket_count; size_t inserted_item_count;};

inline two_way create() {
  bucket *data_start = (bucket *)aligned_alloc(64, sizeof(bucket) * DEFAULT_BUCKET_COUNT);
  memset(data_start, 0xFF, sizeof(bucket) * DEFAULT_BUCKET_COUNT);
  return two_way{.data = data_start, .bucket_count = DEFAULT_BUCKET_COUNT, .inserted_item_count = 0,};
}

inline void destroy(two_way *tw) {free(tw->data);}

inline void unsafe_insert(two_way *tw, key k, value v);

inline void grow(two_way *tw) {
  size_t old_bucket_count = tw->bucket_count;
  bucket* old_data = tw->data;
  tw->inserted_item_count = 0;
  tw->bucket_count *= 2;
  tw->data = (bucket *)aligned_alloc(64, sizeof(bucket) * tw->bucket_count);
  memset(tw->data, 0xFF, sizeof(bucket) * tw->bucket_count);
  for(size_t i = 0; i < old_bucket_count; i++) {
    bucket* s = &old_data[i];
    for(size_t j = 0; j < BUCKET_SIZE && s->keys[j] != EMPTY; j++) {
        unsafe_insert(tw, s->keys[j], s->values[j]);
    }
  }
  free(old_data);
}

// assumes key is not in the map, this can lead to duplicate keys if k is in the map,
// use associate to replace the value of the current key or insert if key not present
inline void unsafe_insert(two_way *tw, key k, value v) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket *bucket_1 = &tw->data[index_1];
  bucket *bucket_2 = &tw->data[index_2];
  size_t n_1 = 0;
  size_t n_2 = 0;
  while(bucket_1->keys[n_1] != EMPTY && ++n_1 < BUCKET_SIZE);
  while(bucket_2->keys[n_2] != EMPTY && ++n_2 < BUCKET_SIZE);
  if(n_1 == BUCKET_SIZE && n_2 == BUCKET_SIZE) {
    grow(tw);
    unsafe_insert(tw, k, v);
    return;
  }
  if(n_1 <= n_2) {
    bucket_1->keys[n_1] = k;
    bucket_1->values[n_1] = v;
  } else {
    bucket_2->keys[n_2] = k;
    bucket_2->values[n_2] = v;
  }
  tw->inserted_item_count++;
}

inline void associate(two_way *tw, key k, value v) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket *bucket_1 = &tw->data[index_1];
  bucket *bucket_2 = &tw->data[index_2];
  size_t i_1 = 0;
  while (i_1 < BUCKET_SIZE) {
    key key_i_1 = bucket_1->keys[i_1];
    if (key_i_1 == k) {
      bucket_1->values[i_1] = v;
      return;
    }
    if (key_i_1 == EMPTY) break;
    i_1++;
  }
  size_t i_2 = 0;
  while (i_2 < BUCKET_SIZE) {
    key key_i_2 = bucket_2->keys[i_2];
    if (key_i_2 == k) {
      bucket_2->values[i_2] = v;
      return;
    }
    if (key_i_2 == EMPTY) break;
    i_2++;
  }
  if(i_1 == BUCKET_SIZE && i_2 == BUCKET_SIZE) {
    grow(tw);
    unsafe_insert(tw, k, v);
    return;
  }
  if(i_1 <= i_2) {
    bucket_1->keys[i_1] = k;
    bucket_1->values[i_1] = v;
  } else {
    bucket_2->keys[i_2] = k;
    bucket_2->values[i_2] = v;
  }
  tw->inserted_item_count++;
}

// assummes tw contains key already, use get, when you don't know if tw contains the key you want
inline value unsafe_get(two_way *tw, key k) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket* bucket_1 = &tw->data[index_1];
  bucket* bucket_2 = &tw->data[index_2];
  for(size_t i = 0;; i++) {
    if(bucket_1->keys[i] == k) return bucket_1->values[i];
    if(bucket_2->keys[i] == k) return bucket_2->values[i];
  }
}

inline value get(two_way *tw, key k) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket* bucket_1 = &tw->data[index_1];
  bucket* bucket_2 = &tw->data[index_2];
  for(size_t i = 0; i < BUCKET_SIZE && !(bucket_1->keys[i] == EMPTY && bucket_2->keys[i] == EMPTY); i++) {
    if(bucket_1->keys[i] == k) return bucket_1->values[i];
    if(bucket_2->keys[i] == k) return bucket_2->values[i];
  }
  return value{};
}

inline bool contains(two_way *tw, key k) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket* bucket_1 = &tw->data[index_1];
  bucket* bucket_2 = &tw->data[index_2];
  for(size_t i = 0; i < BUCKET_SIZE && !(bucket_1->keys[i] == EMPTY && bucket_2->keys[i] == EMPTY); i++) {
    if(bucket_1->keys[i] == k) return true;
    if(bucket_2->keys[i] == k) return true;
  }
  return false;
}

// assummes key is in the table alredy, use dissociate when you don't know if key is in the table
inline void unsafe_dissociate(two_way *tw, key k) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket* bucket_1 = &tw->data[index_1];
  bucket* bucket_2 = &tw->data[index_2];
  for(size_t i = 0;; i++) {
    if(bucket_1->keys[i] == k) {
      for(size_t j = i; j < BUCKET_SIZE - 1; j++) {
        bucket_1->keys[j] = bucket_1->keys[j + 1];
        bucket_1->values[j] = bucket_1->values[j + 1];
      }
      bucket_1->keys[BUCKET_SIZE - 1] = EMPTY;
      tw->inserted_item_count--;
      return;
    }
    if(bucket_2->keys[i] == k) {
      for(size_t j = i; j < BUCKET_SIZE - 1; j++) {
        bucket_2->keys[j] = bucket_2->keys[j + 1];
        bucket_2->values[j] = bucket_2->values[j + 1];
      }
    bucket_2->keys[BUCKET_SIZE - 1] = EMPTY;
    tw->inserted_item_count--;
    return;
    }
  }
}

inline void dissociate(two_way *tw, key k) {
  size_t index_1 = k.high64 & (tw->bucket_count - 1);
  size_t index_2 = k.low64 & (tw->bucket_count - 1);
  bucket* bucket_1 = &tw->data[index_1];
  bucket* bucket_2 = &tw->data[index_2];
  for(size_t i = 0; i < BUCKET_SIZE && !(bucket_1->keys[i] == EMPTY && bucket_2->keys[i] == EMPTY); i++) {
    if (bucket_1->keys[i] == k) {
      for(size_t j = i; j < BUCKET_SIZE - 1; j++) {
        bucket_1->keys[j] = bucket_1->keys[j + 1];
        bucket_1->values[j] = bucket_1->values[j + 1];
      }
      bucket_1->keys[BUCKET_SIZE - 1] = EMPTY;
      tw->inserted_item_count--;
      return;
    }
    if(bucket_2->keys[i] == k) {
      for(size_t j = i; j < BUCKET_SIZE - 1; j++) {
        bucket_2->keys[j] = bucket_2->keys[j + 1];
        bucket_2->values[j] = bucket_2->values[j + 1];
      }
    bucket_2->keys[BUCKET_SIZE - 1] = EMPTY;
    tw->inserted_item_count--;
    return;
    }
  }
}

inline void clear(two_way *tw) {
  tw->inserted_item_count = 0;
  memset(tw->data, 0xFF, sizeof(bucket) * tw->bucket_count);
}

inline size_t count(two_way *tw) { return tw->inserted_item_count; }

inline size_t memory_usage(two_way *tw) { return sizeof(bucket) * tw->bucket_count + sizeof(two_way); }

}
