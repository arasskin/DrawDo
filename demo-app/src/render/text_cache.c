#include "render/text_cache.h"
static uint64_t key_hash(const char *text, float pixels, int width, int height) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
    h ^= *s;
    h *= UINT64_C(1099511628211);
  }
  uint32_t bits;
  if (pixels == 0)
    pixels = 0;
  memcpy(&bits, &pixels, 4);
  h ^= bits;
  h *= UINT64_C(1099511628211);
  h ^= (uint32_t)width;
  h *= UINT64_C(1099511628211);
  h ^= (uint32_t)height;
  return h;
}
void cache_init(text_cache *c, size_t budget, size_t limit, void (*release)(cache_resource *)) {
  *c = (text_cache){0};
  c->budget = budget;
  c->limit = limit;
  c->frame = 1;
  c->release = release;
}
static void unlink_lru(text_cache *c, cache_entry *e) {
  if (e->previous)
    e->previous->next = e->next;
  else
    c->first = e->next;
  if (e->next)
    e->next->previous = e->previous;
  else
    c->last = e->previous;
}
static void prepend(text_cache *c, cache_entry *e) {
  e->previous = NULL;
  e->next = c->first;
  if (c->first)
    c->first->previous = e;
  else
    c->last = e;
  c->first = e;
}
cache_resource *cache_find(text_cache *c, const char *text, float pixels, int width, int height) {
  if (!c->bucket_count)
    return NULL;
  uint64_t hash = key_hash(text, pixels, width, height);
  for (cache_entry *e = c->buckets[hash & (c->bucket_count - 1)]; e; e = e->hash_next)
    if (e->hash == hash && e->pixels == pixels && e->width == width && e->height == height &&
        !strcmp(e->text, text)) {
      if (e->frame != c->frame) {
        e->frame = c->frame;
        c->pinned_bytes += e->bytes;
        c->pinned_count++;
      }
      unlink_lru(c, e);
      prepend(c, e);
      return &e->resource;
    }
  return NULL;
}
static void erase(text_cache *c, cache_entry *e) {
  cache_entry **slot = &c->buckets[e->hash & (c->bucket_count - 1)];
  while (*slot != e)
    slot = &(*slot)->hash_next;
  *slot = e->hash_next;
  unlink_lru(c, e);
  c->bytes -= e->bytes;
  c->count--;
  if (c->release)
    c->release(&e->resource);
  free(e);
}
static bool grow(text_cache *c) {
  if (c->bucket_count && c->count + 1 <= c->bucket_count * 3 / 4)
    return true;
  size_t n = c->bucket_count ? c->bucket_count * 2 : 16;
  if (n < c->bucket_count || n > SIZE_MAX / sizeof(cache_entry *))
    return false;
  cache_entry **b = calloc(n, sizeof(*b));
  if (!b)
    return false;
  for (cache_entry *e = c->first; e; e = e->next) {
    size_t i = e->hash & (n - 1);
    e->hash_next = b[i];
    b[i] = e;
  }
  free(c->buckets);
  c->buckets = b;
  c->bucket_count = n;
  return true;
}
cache_resource *cache_insert(text_cache *c, const char *text, float pixels, int width, int height,
                             size_t bytes, cache_build build, void *ctx) {
  cache_resource *hit = cache_find(c, text, pixels, width, height);
  if (hit)
    return hit;
  if (bytes > c->budget - c->pinned_bytes || c->pinned_count >= c->limit || !grow(c))
    return NULL;
  size_t n = strlen(text);
  if (n > SIZE_MAX - sizeof(cache_entry) - 1)
    return NULL;
  cache_entry *e = calloc(1, sizeof(*e) + n + 1);
  if (!e)
    return NULL;
  while (c->bytes > c->budget - bytes || c->count >= c->limit)
    erase(c, c->last);
  if (!build(ctx, &e->resource)) {
    if (c->release)
      c->release(&e->resource);
    free(e);
    return NULL;
  }
  memcpy(e->text, text, n + 1);
  e->hash = key_hash(text, pixels, width, height);
  e->pixels = pixels;
  e->width = width;
  e->height = height;
  e->bytes = bytes;
  e->frame = c->frame;
  size_t i = e->hash & (c->bucket_count - 1);
  e->hash_next = c->buckets[i];
  c->buckets[i] = e;
  prepend(c, e);
  c->count++;
  c->bytes += bytes;
  c->pinned_bytes += bytes;
  c->pinned_count++;
  return &e->resource;
}
void cache_end_frame(text_cache *c) {
  c->pinned_bytes = c->pinned_count = 0;
  if (++c->frame == 0) {
    for (cache_entry *e = c->first; e; e = e->next)
      e->frame = 0;
    c->frame = 1;
  }
}
void cache_clear(text_cache *c) {
  while (c->last)
    erase(c, c->last);
  free(c->buckets);
  c->buckets = NULL;
  c->bucket_count = 0;
  cache_end_frame(c);
}
