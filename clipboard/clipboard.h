#ifndef CLIPBOARD_H
#define CLIPBOARD_H
#include "containers/bytes.h"
/* Synchronous UTF-8 text operations. Get only in response to a paste action.
 * No text returns an empty string; failure preserves out. Caller frees out.
 * Input must be valid UTF-8 without embedded NUL. Apple: main thread only. */
bool clipboard_set(const char *utf8);
bool clipboard_get(ct_bytes *out);
#endif
