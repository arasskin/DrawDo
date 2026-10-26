#ifndef APP_UI_H
#define APP_UI_H
#include "text_buffers/editor.h"
#include "accessible_ui/access.h"
typedef struct ui_rect {
  float x, y, w, h;
} ui_rect;
static inline bool ui_contains(ui_rect r, float x, float y) {
  return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}
typedef struct ui_surface {
  float width, height;
  void (*shape)(ui_rect, uint32_t, bool);
  void (*text)(const char *, float, float, float, uint32_t, float);
  void (*clip)(ui_rect);
  void (*paragraph)(const char *, ui_rect, float, uint32_t);
  void (*access)(uint64_t, ui_rect, const char *, const char *, unsigned);
} ui_surface;
static inline void ui_access(ui_surface *c, uint64_t id, ui_rect r, const char *label,
                             const char *value, unsigned flags) {
  if (c->access)
    c->access(id, r, label, value, flags);
}
typedef void (*ui_text_callback)(void *, const char *);
typedef struct ui_point {
  float x, y;
} ui_point;
/* Optional initial tap in canvas coordinates; borrowed for this call only. */
typedef void (*ui_editor)(const char *, ui_rect, const ui_point *, ui_text_callback, void *);
#endif
