#include "app.h"
#include "datafriend/c_api.h"
#include <errno.h>
#include <stdio.h>
#ifdef CRDT_DEMO_TIMING
#include "timing.h"
#endif
static bool fail(app *a, const char *message) {
  snprintf(a->error, sizeof(a->error), "%s", message);
  return false;
}
/* A native refresh can race with typing. Change the draft's CRDT base only
 * after ACK; a rejected refresh must leave the old displayed base intact. */
static bool adopt(app *a) {
  if (!a->refreshing || a->adopted || !(a->editor.events & EDITOR_ACK) ||
      (a->editor.events & EDITOR_REJECTED))
    return true;
  bool changed;
  a->adopted = true;
  a->displayed_revision = a->refresh_revision;
  /* Accepted refreshes include every edit already made by this draft. Keep its
   * actor and allocations rather than creating a new Yrs document per refresh. */
  return sequence_merge(&a->displayed, a->refresh.data, a->refresh.size, &changed) ||
         fail(a, "Cannot refresh editor draft");
}
static void edited(void *context, const char *text) {
  app *a = context;
  if (!a->error[0] && adopt(a) && !document_edit(&a->document, &a->displayed, text))
    fail(a, a->document.error);
}
bool app_open(app *a, const char *directory, editor_rect bounds, float font) {
  df_buffer error = {0};
#ifdef CRDT_DEMO_TIMING
  const char *alias = "crdt-demo-timing-v1";
#else
  const char *alias = "crdt-demo-app-v1";
#endif
  a->identity = df_identity_open((const uint8_t *)alias, strlen(alias), 0, &error);
  if (!a->identity) {
    snprintf(a->error, sizeof(a->error), "Identity: %.*s",
             (int)(error.size < 480 ? error.size : 480),
             error.data ? (const char *)error.data : "unavailable");
    df_free(error.data);
    return false;
  }
  df_free(error.data);
  char path[4096], host[64] = "147.182.181.182", peer[65] = "";
  unsigned port = 7777;
  if (strlen(directory) + 32 >= sizeof(path))
    return fail(a, "Application path too long");
  snprintf(path, sizeof(path), "%s/public-key.txt", directory);
  FILE *file = fopen(path, "w");
  if (!file)
    return fail(a, "Cannot export public key");
  const uint8_t *key = df_identity_public(a->identity);
  for (size_t i = 0; i < 32; ++i)
    fprintf(file, "%02x", key[i]);
  fputc('\n', file);
  bool ok = !ferror(file);
  if (fclose(file))
    ok = false;
  if (!ok)
    return fail(a, "Cannot export public key");
  snprintf(path, sizeof(path), "%s/peer.txt", directory);
  file = fopen(path, "r");
  if (file) {
    int count = fscanf(file, "%63s %u %64s", host, &port, peer);
    int trailing;
    do {
      trailing = fgetc(file);
    } while (trailing == ' ' || trailing == '\n' || trailing == '\r');
    fclose(file);
    if (count != 3 || trailing != EOF || !port || port > 65535)
      return fail(a, "Invalid peer.txt configuration");
  } else if (errno != ENOENT) {
    return fail(a, "Cannot read peer.txt");
  }
  if (!document_open(&a->document, directory, a->identity, peer, host, (uint16_t)port))
    return fail(a, a->document.error);
  if (!sequence_fork(&a->document.sequence, &a->displayed))
    return fail(a, "Cannot create editor draft");
  a->displayed_revision = a->document.revision;
  editor_options options = {.bounds = bounds,
                            .font_pixels = font,
                            .foreground = 0x000000ff,
                            .background = 0xffffffff,
                            .multiline = true,
                            .live = true,
                            .label = "Shared text",
                            .embedded = true};
  return editor_open(&a->editor, ct_bytes_string(&a->displayed.text), options, edited, a) ||
         fail(a, "Cannot open native editor");
}
static size_t clamp(const ct_bytes *text, size_t position) {
  if (position > text->size)
    position = text->size;
  while (position && !text_boundary(ct_bytes_string(text), text->size, position))
    --position;
  return position;
}
bool app_step(app *a, editor_rect bounds) {
  if (a->error[0])
    return false;
  editor_poll(&a->editor);
  if ((a->editor.events & EDITOR_ERROR) || !a->editor.active)
    return fail(a, "Native editor stopped");
  if (!adopt(a))
    return false;
  if (a->refreshing && (a->editor.events & EDITOR_ACK)) {
    a->refreshing = a->adopted = false;
    a->refresh.size = 0;
  }
  df_network_pump(a->document.network, 1500);
  if (a->document.error[0])
    return fail(a, a->document.error);
  if (!editor_layout(&a->editor, bounds))
    return fail(a, "Cannot lay out native editor");
  const ct_bytes *text = &a->document.sequence.text;
  /* Even identical visible text may contain new CRDT identities/tombstones.
   * Refresh the draft state whenever idle, not just when the text differs. */
  if (!a->refreshing && !a->editor.buffer.composing &&
      a->displayed_revision != a->document.revision) {
    if (!strcmp(ct_bytes_string(text), ct_bytes_string(&a->editor.buffer.bytes))) {
      ct_bytes state = {0};
      bool changed;
      bool ok = sequence_state(&a->document.sequence, &state) &&
                sequence_merge(&a->displayed, state.data, state.size, &changed);
      ct_bytes_free(&state);
      if (!ok)
        return fail(a, "Cannot merge editor draft");
      a->displayed_revision = a->document.revision;
    } else {
      if (!sequence_state(&a->document.sequence, &a->refresh))
        return fail(a, "Cannot snapshot editor refresh");
      a->refresh_revision = a->document.revision;
#ifdef CRDT_DEMO_TIMING
      text_trace_record(text_trace_tag(ct_bytes_string(text)), TEXT_TRACE_REQUEST,
                        text_trace_clock());
#endif
      a->refreshing =
          editor_set(&a->editor, ct_bytes_string(text), text->size,
                     clamp(text, a->editor.buffer.anchor), clamp(text, a->editor.buffer.caret));
    }
  }
  return !a->error[0];
}
void app_pause(app *a) {
  app_step(a, a->editor.options.bounds);
  if (!document_flush(&a->document))
    fail(a, a->document.error);
}
void app_close(app *a) {
  editor_free(&a->editor);
  document_close(&a->document);
  sequence_close(&a->displayed);
  ct_bytes_free(&a->refresh);
  df_identity_close(a->identity);
  a->identity = NULL;
}
