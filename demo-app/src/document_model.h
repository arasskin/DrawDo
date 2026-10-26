#ifndef APP_DOCUMENT_MODEL_H
#define APP_DOCUMENT_MODEL_H
#include "text_crdt/crdt.h"
#include "datafriend_ui.h"
#include "datafriend/network.h"
#include "profiling/clock_probe.h"
typedef struct document_config {
  char host[64], peer[65], room[65], clock_host[64];
  uint16_t port, clock_port;
  int role, scenario, seed, duration_ms, probe_interval_ms;
  int64_t start_ms;
} document_config;
typedef struct document_edit_sample {
  int64_t when, edit_us;
  uint64_t tick, operations;
  int64_t crdt_us, encode_us, commit_us, reserved_us;
  uint64_t revision;
} document_edit_sample;
typedef struct document_receive_sample {
  int64_t when;
  uint64_t tick, operations;
  int64_t received;
} document_receive_sample;
typedef struct document_model {
  document_config config;
  char actor[65], error[512];
  app_buffer directory;
  document_sequence doc;
  ui_store own, peer;
  df_network *connection;
  df_watch *own_state, *peer_state, *text;
  uint64_t local_tick, peer_tick;
  int action_index;
  bool went_offline, came_online, online, done;
  int64_t merge_us, pump_us;
  clock_probe calibration;
  document_edit_sample *edits;
  size_t edit_count, edit_capacity;
  document_receive_sample *receives;
  size_t receive_count, receive_capacity;
} document_model;
int document_config_load(document_config *, const char *);
bool document_open(document_model *, const char *, const document_config *, void *identity);
void document_close(document_model *);
bool document_text(document_model *, df_buffer *);
bool document_replace(document_model *, const char *);
bool document_replace_draft(document_model *, document_sequence *, const char *);
void document_online(document_model *, bool);
bool document_pump(document_model *);
const char *document_error(const document_model *);
bool document_save(document_model *, void (*extra)(void *, FILE *), void *);
#endif
