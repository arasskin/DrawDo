#ifndef HAPTICS_H
#define HAPTICS_H
enum haptic_kind { HAPTIC_SELECTION, HAPTIC_IMPACT, HAPTIC_SUCCESS };
/* Best effort; respects system settings. Apple: main thread only. */
void haptic_play(enum haptic_kind);
#endif
