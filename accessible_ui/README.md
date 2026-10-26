# Accessible UI

C99 semantic descriptions for a custom renderer. Native adapters expose invisible
UIKit accessibility elements and Android virtual accessibility nodes. No renderer,
layout system, database, or application dependency.

```c
#include "accessible_ui/access.h"
static access_tree tree; /* One app-owned tree, zero-initialized. */

access_begin(&tree);
access_add(&tree, 1, (access_rect){20, 40, 180, 60}, "Save", "", ACCESS_BUTTON);
access_end(&tree);

access_event event;
while (access_poll(&event)) {
  const access_node *node = access_find(&tree, event.id);
  if (node && !(node->flags & ACCESS_DISABLED) && event.action == ACCESS_ACTIVATE) {
    /* Dispatch to the corresponding app action. */
  }
}
/* At shutdown: access_free(&tree); */
```

Check boolean results in application code. IDs must be unique, nonzero and stable
for the same control; removed IDs must not be reused for unrelated controls.
Bounds are physical screen pixels. Labels/values are UTF-8, copied during add.
Use BUTTON, CHECKBOX/CHECKED, SELECTED, DISABLED, HEADER and SCROLL flags as needed.
Scroll actions are FORWARD/BACKWARD. Set only the enabled actions the control supports.

Storage is reused. Native snapshots are published only when something changes.
Native elements retain identity across value/bounds updates. A failed build does
not publish a partial tree. The native thread queues up to 16 discrete actions;
a full queue rejects the action. Poll on the app thread and ignore stale or
otherwise inapplicable IDs. No model pointers cross the native boundary.

Only publish visible, clipped elements. While a native editor is open, the demo
publishes an empty canvas tree and lets the editor expose its own semantics.
This is a flat semantic list: nested collections, custom rotors and general
hardware-keyboard focus traversal are outside this small wrapper.

Link `accessible_ui` and `accessible_ui_native`. Uses `text_buffers` for UTF-8
validation and `containers/bytes.h` for owned storage. See
[platform setup](../native/README.md) and [validation](../docs/validation/native-ui.md).
