#ifndef APP_UI_ACCESS_H
#define APP_UI_ACCESS_H
#include "containers/bytes.h"
typedef struct access_rect {
  float x, y, w, h;
} access_rect;
/* Stable, nonzero IDs. Bounds are physical screen pixels at the platform edge. */
enum access_flags {
  ACCESS_BUTTON = 1,
  ACCESS_CHECKBOX = 2,
  ACCESS_CHECKED = 4,
  ACCESS_SELECTED = 8,
  ACCESS_DISABLED = 16,
  ACCESS_HEADER = 32,
  ACCESS_SCROLL = 64
};
enum access_action { ACCESS_ACTIVATE = 1, ACCESS_FORWARD = 2, ACCESS_BACKWARD = 3 };
typedef struct access_node {
  uint64_t id;
  access_rect bounds;
  unsigned flags;
  size_t label, value; /* Offsets into the snapshot's NUL-terminated UTF-8 strings. */
} access_node;
typedef struct access_snapshot {
  access_node *nodes;
  size_t count, capacity;
  ct_bytes strings;
} access_snapshot;
typedef struct access_tree {
  access_snapshot current, next;
  bool failed, published;
} access_tree;
typedef struct access_event {
  uint64_t id;
  unsigned action;
} access_event;
/* App thread only. Reuses storage; unchanged frames do not cross the native bridge. */
void access_begin(access_tree *);
bool access_add(access_tree *, uint64_t, access_rect, const char *, const char *, unsigned);
bool access_end(access_tree *);
const access_node *access_find(const access_tree *, uint64_t);
void access_free(access_tree *);
/* Discrete actions, delivered on the app thread. Ignore removed/disabled IDs. */
bool access_poll(access_event *);
/* Producer-thread notification: signal the app loop only. NULL unregisters. */
void access_notify(void (*wake)(void));
#endif
