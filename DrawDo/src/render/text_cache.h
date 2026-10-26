#ifndef APP_TEXT_CACHE_H
#define APP_TEXT_CACHE_H
#include "base.h"
typedef struct cache_resource {
  int width, height;
  uint32_t image, view;
  uintptr_t user;
} cache_resource;
typedef struct cache_entry {
  struct cache_entry *previous, *next, *hash_next;
  uint64_t hash, frame;
  size_t bytes;
  float pixels;
  int width, height;
  cache_resource resource;
  char text[];
} cache_entry;
typedef struct text_cache {
  cache_entry **buckets, *first, *last;
  size_t bucket_count, count, budget, limit, bytes, pinned_bytes, pinned_count;
  uint64_t frame;
  void (*release)(cache_resource *);
} text_cache;
typedef bool (*cache_build)(void *, cache_resource *);
void cache_init(text_cache *, size_t, size_t, void (*)(cache_resource *));
cache_resource *cache_find(text_cache *, const char *, float, int, int);
cache_resource *cache_insert(text_cache *, const char *, float, int, int, size_t, cache_build,
                             void *);
void cache_end_frame(text_cache *);
void cache_clear(text_cache *);
#endif
