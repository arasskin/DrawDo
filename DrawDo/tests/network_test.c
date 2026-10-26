#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "model.h"
#include "datafriend/network.h"
#include <poll.h>
#include <time.h>
#include <unistd.h>
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%d: %s\n", __LINE__, #x);                                                   \
      abort();                                                                                     \
    }                                                                                              \
  } while (0)
static uint64_t now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
static void sync_until(df_network *writer, df_network *reader, todo_model *m, size_t count,
                       const char *title, float size, uint32_t color, bool done) {
  uint64_t deadline = now() + 15000000;
  df_network *networks[] = {writer, reader};
  bool runnable[] = {true, true};
  while (now() < deadline) {
    for (size_t i = 0; i < 2; ++i)
      if (networks[i] && runnable[i])
        df_network_pump(networks[i], 1500);
    CHECK(!m->error[0] && todo_mount(m, 1));
    todo_row *row = m->ids.count ? todo_find(m, m->ids.data[0]) : NULL;
    if ((!writer || df_network_settled(writer)) && df_network_settled(reader) &&
        m->ids.count == count && m->style.checkbox_size == size &&
        m->style.checkbox_color == color &&
        (!count || (row && !strcmp(ct_bytes_string(&row->title), title) && row->done == done)))
      return;
    struct pollfd fds[2];
    df_network_watch watches[2];
    uint64_t before = now(), delay = deadline > before ? deadline - before : 0;
    for (size_t i = 0; i < 2; ++i) {
      watches[i] =
          networks[i] ? df_network_prepare(networks[i]) : (df_network_watch){-1, 0, UINT64_MAX};
      fds[i] = (struct pollfd){watches[i].fd, 0, 0};
      if (watches[i].events & DF_NETWORK_READ)
        fds[i].events |= POLLIN;
      if (watches[i].events & DF_NETWORK_WRITE)
        fds[i].events |= POLLOUT;
      if (watches[i].timeout_us < delay)
        delay = watches[i].timeout_us;
    }
    CHECK(poll(fds, 2, (int)((delay + 999) / 1000)) >= 0);
    uint64_t elapsed = now() - before;
    for (size_t i = 0; i < 2; ++i)
      runnable[i] = fds[i].revents || watches[i].timeout_us <= elapsed;
  }
  fprintf(stderr, "writer: %s; reader: %s; rows: %zu; style: %g %08x\n",
          writer ? df_network_error(writer) : "", df_network_error(reader), m->ids.count,
          m->style.checkbox_size, m->style.checkbox_color);
  CHECK(false);
}
static void clean(const char *path) {
  const char *names[] = {"data.mdb", "lock.mdb", "datafriend.owner"};
  for (size_t i = 0; i < COUNT(names); ++i) {
    char f[512];
    snprintf(f, sizeof(f), "%s/%s", path, names[i]);
    unlink(f);
  }
  rmdir(path);
}
int main(int argc, char **argv) {
  CHECK(argc == 3 || argc == 5);
  df_buffer error = {0};
  void *identity = NULL;
  char key[65], root[] = "/tmp/drawdo-sync-XXXXXX", own_path[256], peer_path[256];
  CHECK(mkdtemp(root));
  snprintf(own_path, sizeof(own_path), "%s/own", root);
  snprintf(peer_path, sizeof(peer_path), "%s/peer", root);
  bool observe = argc == 5;
  if (observe) {
    CHECK(strlen(argv[3]) == 64);
    memcpy(key, argv[3], 65);
  } else {
    identity = df_identity_open((const uint8_t *)"drawdo-test", 11, 1, &error);
    CHECK(identity);
    for (size_t i = 0; i < 32; ++i)
      snprintf(key + i * 2, 3, "%02x", df_identity_public(identity)[i]);
  }
  df_store *peer = df_store_open(&(df_options){peer_path, key, DF_ASYNC, 16u << 20}, &error);
  CHECK(peer);
  todo_model replica;
  CHECK(todo_open(&replica, peer, false));
  df_network *reader = df_network_open(argv[1], (uint16_t)atoi(argv[2]), &error);
  CHECK(reader && df_network_follow(reader, peer, &error));
  if (observe) {
    sync_until(NULL, reader, &replica, 1, argv[4], 26, 0xf44336ff, false);
    puts("PASS: Android namespace, task and persisted UI settings received from deployed server");
  } else {
    df_store *own = df_store_open(&(df_options){own_path, key, DF_ASYNC, 16u << 20}, &error);
    CHECK(own);
    todo_model model;
    CHECK(todo_open(&model, own, true));
    uint32_t id = todo_add(&model, "Sync task");
    CHECK(id);
    df_network *writer = df_network_open(argv[1], (uint16_t)atoi(argv[2]), &error);
    CHECK(writer && df_network_push(writer, own, identity, &error));
    sync_until(writer, reader, &replica, 1, "Sync task", 26, 0xf44336ff, false);
    CHECK(todo_title(&model, id, "Renamed over network") && todo_toggle(&model, id));
    CHECK(todo_set_style(&model, TODO_UI_CHECKBOX_SIZE, df_real(34)));
    CHECK(todo_set_style(&model, TODO_UI_CHECKBOX_COLOR, df_int(0x2266ccff)));
    sync_until(writer, reader, &replica, 1, "Renamed over network", 34, 0x2266ccff, true);
    CHECK(todo_title(&model, id, ""));
    sync_until(writer, reader, &replica, 0, "", 34, 0x2266ccff, false);
    df_network_close(writer);
    todo_close(&model);
    CHECK(df_store_close(own, &error) == DF_OK);
    clean(own_path);
    puts("PASS: owner push, subscriber updates, task edit/toggle/delete, checkbox size and color "
         "sync");
  }
  df_network_close(reader);
  todo_close(&replica);
  CHECK(df_store_close(peer, &error) == DF_OK);
  df_identity_close(identity);
  df_free(error.data);
  clean(peer_path);
  rmdir(root);
}
