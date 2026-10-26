#ifndef NATIVE_LOOP_H
#define NATIVE_LOOP_H
#include "datafriend/network.h"
#include <stdbool.h>
/* Open/watch/close run on the app's existing owner thread. Wake is thread-safe,
 * coalesces notifications, and never invokes process inline. */
bool loop_open(void (*process)(void));
bool loop_watch(df_network_watch);
void loop_wake(void);
void loop_close(void);
#endif
