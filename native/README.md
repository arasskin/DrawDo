# Native event loop

`loop.h` is the small shared owner-thread loop adapter for DrawDo and the CRDT demo. Android uses the existing Sokol ALooper plus eventfd/timerfd; Apple uses the main CFRunLoop. Neither adapter creates another model thread.

Open it on the owning thread, register the network's `df_network_prepare()` result after processing, and use `loop_wake()` for cross-thread mailbox notifications. Wake callbacks must only signal the loop. Unregister producers before closing it.
