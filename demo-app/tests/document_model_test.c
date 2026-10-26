#include "document_model.h"
#include "test.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
static int listener(uint16_t *port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  CHECK(fd >= 0);
  struct sockaddr_in address = {0};
  address.sin_family = AF_INET;
  CHECK(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
  CHECK(!bind(fd, (struct sockaddr *)&address, sizeof(address)) && !listen(fd, 2));
  socklen_t size = sizeof(address);
  CHECK(!getsockname(fd, (struct sockaddr *)&address, &size));
  *port = ntohs(address.sin_port);
  CHECK(!fcntl(fd, F_SETFL, O_NONBLOCK));
  return fd;
}
static void tick(document_model *m) {
  CHECK(document_pump(m));
  const struct timespec pause = {0, 1000000};
  nanosleep(&pause, NULL);
}
static int accept_client(document_model *m, int fd) {
  int peer = -1;
  int64_t deadline = app_us() + 3000000;
  do {
    tick(m);
    peer = accept(fd, NULL, NULL);
  } while (peer < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && app_us() < deadline);
  CHECK(peer >= 0);
  tick(m);
  CHECK(df_network_connected(m->connection));
  return peer;
}
static void reconnect_status(document_model *m, int fd) {
  document_online(m, true);
  int peer = accept_client(m, fd);
  CHECK(!*document_error(m));
  /* A transport EOF, followed by a new connection, must not leave a stale
   * document error. This listener deliberately does not implement sync. */
  CHECK(!shutdown(peer, SHUT_RDWR));
  close(peer);
  int64_t deadline = app_us() + 1000000;
  do {
    tick(m);
  } while (df_network_connected(m->connection) && app_us() < deadline);
  CHECK(!df_network_connected(m->connection) && *document_error(m));
  peer = accept_client(m, fd);
  CHECK(!*document_error(m));
  strcpy(m->error, "Document write failed");
  tick(m);
  CHECK(!strcmp(document_error(m), "Document write failed"));
  m->error[0] = 0;
  document_online(m, false);
  close(peer);
}
static void *identity(void) {
  df_buffer b = {0};
  void *i = df_identity_open((const uint8_t *)"test", 4, 1, &b);
  df_free(b.data);
  CHECK(i);
  return i;
}
static void equal(document_model *m, const char *value) {
  df_buffer b;
  CHECK(document_text(m, &b) && b.size == strlen(value) && !memcmp(b.data, value, b.size));
}
int main(void) {
  char path[128];
  test_path(path, sizeof(path), "document-c99");
  void *a = identity(), *b = identity();
  document_config c = {0};
  strcpy(c.host, "127.0.0.1");
  strcpy(c.room, "state-fact");
  int server = listener(&c.port);
  const uint8_t *key = df_identity_public(b);
  for (int i = 0; i < 32; i++)
    snprintf(c.peer + i * 2, 3, "%02x", key[i]);
  document_model m;
  if (!document_open(&m, path, &c, a)) {
    fprintf(stderr, "open: %s\n", m.error);
    return 1;
  }
  document_online(&m, false);
  reconnect_status(&m, server);
  equal(&m, "");
  CHECK(df_view_size(m.text->result) == 1);
  CHECK(document_replace(&m, "Hello 🌍"));
  document_sequence draft;
  CHECK(sequence_fork(&m.doc, &draft));
  CHECK(document_replace_draft(&m, &draft, "Hello 🌍!") &&
        document_replace_draft(&m, &draft, "Hello 🌍!!"));
  equal(&m, "Hello 🌍!!");
  CHECK(df_recomputed(m.own.engine) == 2);
  df_watch *all = ui_subscribe(&m.own, NULL, 0, true, NULL, NULL);
  CHECK(all && df_view_size(all->result) == 2);
  ui_unwatch(&m.own, &all);
  sequence_close(&draft);
  document_close(&m);
  CHECK(document_open(&m, path, &c, a));
  document_online(&m, false);
  equal(&m, "Hello 🌍!!");
  CHECK(document_replace(&m, ""));
  equal(&m, "");
  CHECK(df_view_size(m.text->result) == 1);
  CHECK(document_save(&m, NULL, NULL));
  document_close(&m);
  CHECK(document_open(&m, path, &c, a));
  equal(&m, "");
  document_close(&m);
  df_identity_close(a);
  df_identity_close(b);
  close(server);
  CHECK(app_remove_tree(path));
  puts("PASS: one-fact UI, two-fact storage, repeated editor, persisted state and deletion");
  return 0;
}
