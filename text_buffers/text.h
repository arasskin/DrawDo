#ifndef APP_UI_TEXT_H
#define APP_UI_TEXT_H
#include "containers/bytes.h"
/* All public positions are UTF-8 byte boundaries. Native adapters translate
 * UTF-16 explicitly. User-visible grapheme/word movement belongs to the native
 * editor; move_to sets an exact logical position, not a visual column. */
typedef enum text_affinity { TEXT_BEFORE, TEXT_AFTER } text_affinity;
typedef struct text_buffer {
  ct_bytes bytes;
  size_t anchor, caret;
  text_affinity anchor_affinity, caret_affinity;
  size_t marked_start, marked_end;
  bool composing;
  uint64_t revision;
} text_buffer;
bool text_valid(const char *text, size_t size);
bool text_boundary(const char *text, size_t size, size_t byte);
bool text_to_utf16(const char *text, size_t size, size_t byte, size_t *units);
bool text_from_utf16(const char *text, size_t size, size_t units, size_t *byte);
/* Set resets selection to end and clears composition. Input must not alias the
 * buffer for set/replace. Failed operations leave content and selection intact. */
bool text_buffer_set(text_buffer *, const char *, size_t);
bool text_buffer_select(text_buffer *, size_t anchor, size_t caret);
bool text_buffer_move_to(text_buffer *, size_t byte, bool extend);
bool text_buffer_mark(text_buffer *, size_t start, size_t end, bool composing);
/* Replace [start,end), remapping selection with explicit endpoint affinities.
 * Programmatic replacements during native composition are rejected. */
bool text_buffer_replace(text_buffer *, size_t start, size_t end, const char *, size_t);
/* Replaces the selected range and collapses the caret after inserted text. */
bool text_buffer_insert(text_buffer *, const char *, size_t);
void text_buffer_free(text_buffer *);
#endif
