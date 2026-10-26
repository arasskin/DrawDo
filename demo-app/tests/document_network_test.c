#include "document_model.h"
#include "test.h"
static bool equal(document_model *a, document_model *b) {
  df_buffer x, y;
  return document_text(a, &x) && document_text(b, &y) && x.size == y.size &&
         !memcmp(x.data, y.data, x.size);
}
static void wait_for(document_model *a, document_model *b) {
  int64_t deadline = app_us() + 15000000;
  do {
    CHECK(document_pump(a) && document_pump(b));
    struct timespec t = {0, 1000000};
    nanosleep(&t, NULL);
    if (app_us() > deadline) {
      fprintf(stderr, "Convergence timeout: %s / %s\n", a->error, b->error);
      exit(1);
    }
  } while (!equal(a, b) || !df_network_settled(a->connection) ||
           !df_network_settled(b->connection));
}
int main(int argc, char **argv) {
  CHECK(argc == 3);
  char root[128], pa[160], pb[160];
  test_path(root, sizeof(root), "doc-net-c99");
  snprintf(pa, sizeof(pa), "%s/a", root);
  snprintf(pb, sizeof(pb), "%s/b", root);
  df_buffer out = {0};
  void *ia = df_identity_open((const uint8_t *)"test", 4, 1, &out);
  df_free(out.data);
  out = (df_buffer){0};
  void *ib = df_identity_open((const uint8_t *)"test", 4, 1, &out);
  df_free(out.data);
  CHECK(ia && ib);
  document_config ca = {0}, cb = {0};
  snprintf(ca.host, sizeof(ca.host), "%s", argv[1]);
  ca.port = (uint16_t)atoi(argv[2]);
  strcpy(ca.room, "host");
  cb = ca;
  for (int i = 0; i < 32; i++) {
    snprintf(ca.peer + i * 2, 3, "%02x", df_identity_public(ib)[i]);
    snprintf(cb.peer + i * 2, 3, "%02x", df_identity_public(ia)[i]);
  }
  ca.seed = 1000;
  cb.role = 1;
  document_model left, right;
  CHECK(document_open(&left, pa, &ca, ia) && document_open(&right, pb, &cb, ib));
  wait_for(&left, &right);
  CHECK(df_view_size(left.text->result) == 1 && df_view_size(right.text->result) == 1);
  document_online(&left, false);
  document_online(&right, false);
  CHECK(document_replace(&left, "abc 🌍") && document_replace(&right, "def 🌏"));
  document_online(&left, true);
  document_online(&right, true);
  wait_for(&left, &right);
  document_sequence draft;
  CHECK(sequence_fork(&left.doc, &draft));
  CHECK(document_replace_draft(&left, &draft, "first"));
  df_buffer text;
  CHECK(document_text(&right, &text));
  app_buffer next = {0};
  CHECK(app_append(&next, text.data, text.size) && app_append(&next, " remote", 7) &&
        document_replace(&right, app_cstr(&next)));
  wait_for(&left, &right);
  CHECK(document_replace_draft(&left, &draft, "first!"));
  wait_for(&left, &right);
  CHECK(strstr(app_cstr(&left.doc.text), "remote"));
  sequence_close(&draft);
  app_buffer_free(&next);
  document_close(&left);
  document_close(&right);
  df_identity_close(ia);
  df_identity_close(ib);
  CHECK(app_remove_tree(root));
  puts("PASS: TCP document sync, seed, offline replacement and live stale draft");
  return 0;
}
