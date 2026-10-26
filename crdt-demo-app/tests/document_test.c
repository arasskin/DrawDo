#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "document.h"
#include "datafriend/c_api.h"
#include <stdio.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                      \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
static void snapshot(document *d, ct_bytes *state) { CHECK(sequence_state(&d->sequence, state)); }
static void converge(document *a, document *b) {
  ct_bytes left = {0}, right = {0};
  snapshot(a, &left);
  snapshot(b, &right);
  CHECK(document_merge(a, right.data, right.size));
  CHECK(document_merge(b, left.data, left.size));
  CHECK(!strcmp(ct_bytes_string(&a->sequence.text), ct_bytes_string(&b->sequence.text)));
  ct_bytes_free(&left);
  ct_bytes_free(&right);
}
int main(void) {
  char directory[] = "/tmp/crdt-demo-test-XXXXXX", paths[2][256], keys[2][65];
  CHECK(mkdtemp(directory));
  document docs[2] = {0};
  void *identities[2];
  for (int i = 0; i < 2; ++i) {
    df_buffer error = {0};
    identities[i] = df_identity_open(NULL, 0, 1, &error);
    CHECK(identities[i]);
    df_free(error.data);
    for (size_t j = 0; j < 32; ++j)
      snprintf(keys[i] + j * 2, 3, "%02x", df_identity_public(identities[i])[j]);
    snprintf(paths[i], sizeof(paths[i]), "%s/%d", directory, i);
  }
  for (int i = 0; i < 2; ++i)
    CHECK(document_open(&docs[i], paths[i], identities[i], keys[1 - i], "", 0));
  document_sequence draft[2] = {0};
  CHECK(sequence_fork(&docs[0].sequence, &draft[0]));
  CHECK(document_edit(&docs[0], &draft[0], "Hello 🌍"));
  converge(&docs[0], &docs[1]);
  sequence_close(&draft[0]);
  CHECK(sequence_fork(&docs[0].sequence, &draft[0]));
  CHECK(sequence_fork(&docs[1].sequence, &draft[1]));
  /* Offline edits from the same base: both survive reconciliation. */
  CHECK(document_edit(&docs[0], &draft[0], "Hello Android 🌍"));
  CHECK(document_edit(&docs[1], &draft[1], "Hello 🌍 iPhone"));
  converge(&docs[0], &docs[1]);
  CHECK(!strcmp(ct_bytes_string(&docs[0].sequence.text), "Hello Android 🌍 iPhone"));
  /* A user types before the remote editor refresh arrives: apply to the
   * displayed draft, preserving the unseen remote insertion. */
  CHECK(document_edit(&docs[0], &draft[0], "Hello Android 🌍!"));
  converge(&docs[0], &docs[1]);
  CHECK(strstr(ct_bytes_string(&docs[0].sequence.text), "iPhone"));
  CHECK(strstr(ct_bytes_string(&docs[0].sequence.text), "!"));
  ct_bytes state = {0}, text = {0};
  snapshot(&docs[0], &state);
  CHECK(ct_bytes_set(&text, ct_bytes_string(&docs[0].sequence.text), docs[0].sequence.text.size));
  uint64_t revision = docs[0].revision;
  CHECK(document_merge(&docs[0], state.data, state.size));
  CHECK(docs[0].revision == revision); /* Echoes do not republish forever. */
  /* Exercise actual peer watch delivery and an absent peer record. */
  df_edit edit = {0};
  CHECK(df_edit_set(&edit, 1, DF_ATTR_DOCUMENT_STATE, df_bytes_value(state.data, state.size)));
  CHECK(df_edit_apply(docs[0].peer, &edit) == DF_OK);
  CHECK(df_edit_unset(&edit, 1, DF_ATTR_DOCUMENT_STATE));
  CHECK(df_edit_apply(docs[0].peer, &edit) == DF_OK);
  CHECK(!strcmp(ct_bytes_string(&docs[0].sequence.text), ct_bytes_string(&text)));
  df_edit_free(&edit);
  for (int i = 0; i < 2; ++i) {
    CHECK(document_flush(&docs[i]));
    document_close(&docs[i]);
    CHECK(document_open(&docs[i], paths[i], identities[i], keys[1 - i], "", 0));
    CHECK(!strcmp(ct_bytes_string(&docs[i].sequence.text), ct_bytes_string(&text)));
  }
  sequence_close(&draft[0]);
  CHECK(sequence_fork(&docs[0].sequence, &draft[0]));
  CHECK(document_edit(&docs[0], &draft[0], ""));
  converge(&docs[0], &docs[1]);
  CHECK(docs[1].sequence.text.size == 0); /* Explicit text deletion propagates. */
  for (int i = 0; i < 2; ++i) {
    sequence_close(&draft[i]);
    document_close(&docs[i]);
    df_identity_close(identities[i]);
  }
  ct_bytes_free(&state);
  ct_bytes_free(&text);
  printf(
      "PASS: concurrent edits, stale display, idempotence, absent peer, persistence, deletion\n");
  return 0;
}
