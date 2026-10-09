#pragma once
// The todo app proper: datafriend Db as the source of truth, rendered
// immediate-mode from the frame loop, persisted + mirrored through the
// glue/store/sync stack. Threading per the settled model: main thread owns
// the Db and writes the store; one sync worker mirrors up on a dirty signal.

void todo_app_init(void);      // after sg_setup + text_init
void todo_app_frame(void);     // inside the render pass, before text_flush
void todo_app_touch(float x_logical, float y_logical);
void todo_app_shutdown(void);  // before text_shutdown
