# Clipboard

A small synchronous C99 interface to the system text clipboard:

```c
#include "clipboard/clipboard.h"
clipboard_set("Hello 🌱");
ct_bytes text = {0};
if (clipboard_get(&text)) {
  /* Consume ct_bytes_string(&text); size excludes the terminating NUL. */
}
ct_bytes_free(&text);
```

Check return values. Text is UTF-8 without embedded NUL. An empty or non-text
clipboard returns an empty string. Failure preserves the output buffer; set input
must not alias a buffer being modified. Clipboard items other than text are not
coerced or opened. The system may restrict clipboard access or ask for permission.
Read only in response to an explicit paste action, never on each frame.

Apple calls run on the main thread. Android calls use the clipboard service
synchronously; no app UI queue/acknowledgement protocol is involved. The app must
be in the foreground for reading to work reliably. Link `clipboard`; it uses
`text_buffers` for UTF-8 validation and the common byte container.
See [platform setup](../native/README.md).
