# Haptics

```c
#include "haptics/haptics.h"
haptic_play(HAPTIC_SELECTION); /* Small change in selection. */
haptic_play(HAPTIC_IMPACT);    /* Light impact. */
haptic_play(HAPTIC_SUCCESS);   /* Completed action. */
```

Best effort, with no acknowledgement protocol or custom waveform API. Native
settings and hardware decide whether feedback occurs. Apple calls run on the
main thread and reuse feedback generators. Android posts View haptic feedback
to the activity's UI thread, uses platform fallbacks on older releases and does
not request vibration permission or bypass user settings.

Link `haptics`. See [platform setup](../native/README.md). Device automation checks
request dispatch; it cannot verify the sensation experienced by a person.
