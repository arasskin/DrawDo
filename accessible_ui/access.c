#include "accessible_ui/native.h"
#include "text_buffers/text.h"
#include <math.h>
void access_begin(access_tree *tree) {
  tree->next.count = tree->next.strings.size = 0;
  tree->failed = false;
}
bool access_add(access_tree *tree, uint64_t id, access_rect bounds, const char *label,
                const char *value, unsigned flags) {
  access_snapshot *s = &tree->next;
  if (tree->failed)
    return false;
  if (!id || !label || !isfinite(bounds.x) || !isfinite(bounds.y) || !isfinite(bounds.w) ||
      !isfinite(bounds.h) || bounds.w <= 0 || bounds.h <= 0)
    goto fail;
  for (size_t i = 0; i < s->count; i++)
    if (s->nodes[i].id == id)
      goto fail;
  if (!value)
    value = "";
  if (!text_valid(label, strlen(label)) || !text_valid(value, strlen(value)) ||
      !ct_array_reserve(&s->nodes, &s->capacity, s->count + 1, sizeof(*s->nodes)))
    goto fail;
  access_node node = {0};
  node.id = id;
  node.bounds = bounds;
  node.flags = flags;
  node.label = s->strings.size;
  if (!ct_bytes_append(&s->strings, label, strlen(label) + 1))
    goto fail;
  node.value = s->strings.size;
  if (!ct_bytes_append(&s->strings, value, strlen(value) + 1))
    goto fail;
  s->nodes[s->count++] = node;
  return true;
fail:
  tree->failed = true;
  return false;
}
static bool equal(const access_snapshot *a, const access_snapshot *b) {
  if (a->count != b->count || a->strings.size != b->strings.size)
    return false;
  for (size_t i = 0; i < a->count; i++) {
    const access_node *x = &a->nodes[i], *y = &b->nodes[i];
    if (x->id != y->id || x->flags != y->flags || x->label != y->label || x->value != y->value ||
        x->bounds.x != y->bounds.x || x->bounds.y != y->bounds.y || x->bounds.w != y->bounds.w ||
        x->bounds.h != y->bounds.h)
      return false;
  }
  return !a->strings.size || !memcmp(a->strings.data, b->strings.data, a->strings.size);
}
bool access_end(access_tree *tree) {
  if (tree->failed)
    return false;
  if (tree->published && equal(&tree->next, &tree->current))
    return true;
  if (!platform_accessibility_set(&tree->next))
    return false;
  access_snapshot old = tree->current;
  tree->current = tree->next;
  tree->next = old;
  tree->published = true;
  return true;
}
const access_node *access_find(const access_tree *tree, uint64_t id) {
  for (size_t i = 0; i < tree->current.count; i++)
    if (tree->current.nodes[i].id == id)
      return &tree->current.nodes[i];
  return NULL;
}
void access_free(access_tree *tree) {
  access_snapshot empty = {0};
  platform_accessibility_set(&empty);
  free(tree->current.nodes);
  free(tree->next.nodes);
  ct_bytes_free(&tree->current.strings);
  ct_bytes_free(&tree->next.strings);
  memset(tree, 0, sizeof(*tree));
}
