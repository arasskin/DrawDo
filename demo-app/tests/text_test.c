#include "text_buffers/text.h"
#include "test.h"
int main(void) {
  text_buffer b = {0};
  const char *unicode = "a🌍é世界";
  size_t n = strlen(unicode), offsets[] = {0, 1, 5, 6, 8, 11, 14};
  size_t utf16[] = {0, 1, 3, 4, 5, 6, 7};
  CHECK(text_buffer_set(&b, unicode, n));
  for (size_t i = 0; i < COUNT(offsets); ++i) {
    size_t value;
    CHECK(text_to_utf16(unicode, n, offsets[i], &value) && value == utf16[i]);
    CHECK(text_from_utf16(unicode, n, utf16[i], &value) && value == offsets[i]);
  }
  size_t ignored;
  CHECK(!text_from_utf16(unicode, n, 2, &ignored)); /* Half of the emoji surrogate pair. */
  CHECK(!text_to_utf16(unicode, n, 2, &ignored));
  CHECK(!text_buffer_select(&b, 2, 5));
  CHECK(text_buffer_select(&b, 5, 1)); /* Preserve selection direction. */
  CHECK(text_buffer_insert(&b, "🌱", 4));
  CHECK(!strcmp(app_cstr(&b.bytes), "a🌱é世界") && b.anchor == 5 && b.caret == 5);
  CHECK(text_buffer_move_to(&b, 1, false));
  CHECK(text_buffer_move_to(&b, 8, true) && b.anchor == 1 && b.caret == 8);
  CHECK(text_buffer_replace(&b, 0, 0, "Hi ", 3));
  CHECK(b.anchor == 4 && b.caret == 11);
  CHECK(text_buffer_replace(&b, 4, 8, "", 0));
  CHECK(b.anchor == 4 && b.caret == 7);
  CHECK(text_buffer_mark(&b, 4, 7, true));
  uint64_t revision = b.revision;
  CHECK(!text_buffer_replace(&b, 0, 0, "x", 1) && b.revision == revision);
  CHECK(text_buffer_mark(&b, 0, 0, false));
  CHECK(text_buffer_set(&b, "ab", 2));
  CHECK(text_buffer_select(&b, 1, 1));
  b.anchor_affinity = b.caret_affinity = TEXT_BEFORE;
  CHECK(text_buffer_replace(&b, 1, 1, "X", 1) && b.caret == 1);
  b.anchor_affinity = b.caret_affinity = TEXT_AFTER;
  CHECK(text_buffer_replace(&b, 1, 1, "Y", 1) && b.caret == 2);
  CHECK(text_buffer_replace(&b, 0, b.bytes.size, "", 0) && b.caret == 0 && b.anchor == 0);
  CHECK(text_buffer_insert(&b, "hello", 5));
  CHECK(!text_buffer_set(&b, "\xc0\x80", 2));
  CHECK(!text_buffer_set(&b, "\xed\xa0\x80", 3));
  CHECK(!text_buffer_set(&b, "\xf4\x90\x80\x80", 4));
  CHECK(!text_buffer_set(&b, "a\0b", 3));
  CHECK(!strcmp(app_cstr(&b.bytes), "hello"));
  CHECK(!text_buffer_replace(&b, 5, 1, "x", 1));
  CHECK(!text_buffer_replace(&b, 0, 1, "\xf0", 1));
  CHECK(!text_buffer_replace(&b, 0, 1, "x", SIZE_MAX));
  CHECK(text_buffer_set(&b, "", 0) && b.caret == 0);
  text_buffer_free(&b);
  puts("PASS text buffer: UTF-8/UTF-16, directional selections, affinity, edits and composition");
  return 0;
}
