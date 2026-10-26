#include "render/text_cache.h"
#include "test.h"
static unsigned built, released;
static bool build(void *ctx, cache_resource *r) {
  (void)ctx;
  r->user = ++built;
  return true;
}
static bool broken(void *ctx, cache_resource *r) {
  (void)ctx;
  (void)r;
  return false;
}
static void release(cache_resource *r) {
  if (r->user)
    released++;
}
int main(void) {
  text_cache c;
  cache_init(&c, 100, 1000, release);
  cache_resource *a = cache_insert(&c, "hello", 12, 0, 0, 40, build, NULL);
  CHECK(a && built == 1 && c.bytes == 40);
  CHECK(cache_find(&c, "hello", 12, 0, 0) == a);
  CHECK(cache_insert(&c, "hello", 12, 0, 0, 40, build, NULL) == a && built == 1);
  CHECK(cache_insert(&c, "world", 12, 0, 0, 40, build, NULL));
  CHECK(!cache_insert(&c, "pinned", 12, 0, 0, 30, build, NULL));
  CHECK(c.count == 2 && c.bytes == 80);
  cache_end_frame(&c);
  CHECK(cache_find(&c, "hello", 12, 0, 0) == a);
  CHECK(cache_insert(&c, "new", 12, 0, 0, 40, build, NULL));
  CHECK(!cache_find(&c, "world", 12, 0, 0) && released == 1);
  cache_end_frame(&c);
  CHECK(!cache_insert(&c, "broken", 12, 0, 0, 1, broken, NULL));
  CHECK(c.count == 2 && c.bytes == 80);
  cache_clear(&c);
  CHECK(released == built);
  cache_init(&c, 1000000, 1000, release);
  for (int i = 0; i < 500; i++) {
    char name[32];
    snprintf(name, sizeof(name), "label %d", i);
    CHECK(cache_insert(&c, name, 12, 10, 0, 100, build, NULL));
  }
  CHECK(c.count == 500 && c.bytes == 50000);
  cache_end_frame(&c);
  for (int i = 499; i >= 0; i--) {
    char name[32];
    snprintf(name, sizeof(name), "label %d", i);
    CHECK(cache_find(&c, name, 12, 10, 0));
  }
  CHECK(!cache_find(&c, "label 1", 13, 10, 0) && !cache_find(&c, "label 1", 12, 11, 0));
  CHECK(!cache_find(&c, "label 1", 12, 10, 60));
  CHECK(cache_insert(&c, "label 1", 12, 10, 60, 100, build, NULL));
  CHECK(cache_find(&c, "label 1", 12, 10, 60) != cache_find(&c, "label 1", 12, 10, 0));
  CHECK(!cache_find(&c, "label 1", 12, 10, 61));
  cache_clear(&c);
  CHECK(built == released);
  cache_init(&c, 100, 1, release);
  CHECK(cache_insert(&c, "one", 12, 0, 0, 1, build, NULL));
  CHECK(!cache_insert(&c, "two", 12, 0, 0, 1, build, NULL));
  cache_end_frame(&c);
  CHECK(cache_insert(&c, "two", 12, 0, 0, 1, build, NULL));
  cache_clear(&c);
  CHECK(released == built);
  puts("PASS: cache reuse, pinned LRU eviction, growth, raster keys, limits, failed build cleanup");
  return 0;
}
