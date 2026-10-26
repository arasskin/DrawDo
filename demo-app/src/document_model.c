#include "document_model.h"
static bool fail(document_model *m, const char *error) {
  snprintf(m->error, sizeof(m->error), "%s", error);
  return false;
}
int document_config_load(document_config *c, const char *directory) {
  memset(c, 0, sizeof(*c));
  strcpy(c->host, "192.168.1.19");
  strcpy(c->room, "shared");
  c->port = 17879;
  c->duration_ms = 30000;
  app_buffer path = {0};
  if (!app_format(&path, "%s/document-config.txt", directory))
    return -1;
  FILE *f = fopen(app_cstr(&path), "r");
  app_buffer_free(&path);
  if (!f)
    return errno == ENOENT ? 0 : -1;
  unsigned port = 0, clock_port = 0;
  long long start = 0;
  int n = fscanf(f, "%63s %u %64s %64s %d %d %d %lld %d", c->host, &port, c->peer, c->room,
                 &c->role, &c->scenario, &c->seed, &start, &c->duration_ms);
  c->start_ms = start;
  bool ok = n == 9 && port > 0 && port <= 65535 && strlen(c->peer) == 64 &&
            strspn(c->peer, "0123456789abcdefABCDEF") == 64 && strlen(c->room) > 0 &&
            strspn(c->room, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                strlen(c->room) &&
            c->role >= 0 && c->role <= 1 && c->scenario >= 0 && c->scenario <= 3 && c->seed >= 0 &&
            c->seed <= 20000 && c->duration_ms >= 5000 && c->duration_ms <= 180000;
  c->port = (uint16_t)port;
  int optional = fscanf(f, "%d", &c->probe_interval_ms);
  if (optional == 1)
    ok = ok && c->probe_interval_ms >= 0 && c->probe_interval_ms <= 10000;
  else if (optional != EOF)
    ok = false;
  optional = fscanf(f, "%63s %u", c->clock_host, &clock_port);
  if (optional != EOF)
    ok = ok && optional == 2 && clock_port > 0 && clock_port <= 65535;
  c->clock_port = (uint16_t)clock_port;
  fclose(f);
  return ok ? 1 : -1;
}
static bool state_value(document_model *m, df_watch *w, uint64_t *tick, df_buffer *state) {
  *tick = 0;
  *state = (df_buffer){0};
  df_buffer b;
  if (!ui_result_bytes(w->result, 1, 35, &b))
    return true;
  app_reader r = {b.data, b.size, false};
  const uint8_t *magic = app_take(&r, 4);
  if (!magic || memcmp(magic, "YDF2", 4))
    return fail(m, "Unsupported document format");
  size_t room_size, state_size;
  const uint8_t *room = app_read_blob(&r, &room_size);
  uint64_t t = app_read(&r, 8);
  const uint8_t *bytes = app_read_blob(&r, &state_size);
  if (r.failed || r.size)
    return fail(m, "Invalid document state record");
  if (room_size != strlen(m->config.room) || memcmp(room, m->config.room, room_size))
    return true;
  *tick = t;
  *state = (df_buffer){(uint8_t *)bytes, state_size};
  return true;
}
static bool restore(document_model *m) {
  sequence_close(&m->doc);
  if (!sequence_open(&m->doc))
    return fail(m, "Document allocation failed");
  df_buffer a, b;
  uint64_t at, bt;
  bool changed;
  if (!state_value(m, m->own_state, &at, &a) || !state_value(m, m->peer_state, &bt, &b))
    return false;
  if ((a.size && !sequence_merge(&m->doc, a.data, a.size, &changed)) ||
      (b.size && !sequence_merge(&m->doc, b.data, b.size, &changed)))
    return fail(m, "Invalid persisted CRDT state");
  m->local_tick = at;
  m->peer_tick = bt;
  return true;
}
static bool publish(document_model *m, uint64_t tick, const app_buffer *state) {
  app_buffer record = {0};
  df_edit edit = {0};
  bool ok = app_append(&record, "YDF2", 4) &&
            app_blob(&record, m->config.room, strlen(m->config.room)) && app_u64(&record, tick) &&
            app_blob(&record, state->data, state->size) &&
            df_edit_set(&edit, 1, 35, df_encoded(record.data, record.size)) &&
            df_edit_set(&edit, 1, 34, df_encoded(m->doc.text.data, m->doc.text.size));
  if (ok)
    ok = ui_change(&m->own, &edit);
  df_edit_free(&edit);
  app_buffer_free(&record);
  if (!ok) {
    restore(m);
    return fail(m, m->own.error[0] ? m->own.error : "Document publication failed");
  }
  m->local_tick = tick;
  return true;
}
static bool publish_current(document_model *m, uint64_t tick) {
  app_buffer state = {0};
  bool ok = sequence_state(&m->doc, &state) && publish(m, tick, &state);
  app_buffer_free(&state);
  return ok;
}
static void remote(void *ctx, df_view *result) {
  document_model *m = ctx;
  if (m->peer_state && result == m->peer_state->result) {
    int64_t start = app_us();
    uint64_t tick;
    df_buffer state;
    if (!state_value(m, m->peer_state, &tick, &state))
      return;
    if (state.size) {
      uint64_t previous = m->peer_tick;
      bool modified;
      if (!sequence_merge(&m->doc, state.data, state.size, &modified)) {
        fail(m, "Invalid remote CRDT state");
        return;
      }
      if (tick > m->peer_tick)
        m->peer_tick = tick;
      if (modified && !publish_current(m, m->local_tick))
        return;
      if (tick > previous) {
        if (!app_reserve(&m->receives, &m->receive_capacity, m->receive_count + 1,
                         sizeof(*m->receives))) {
          fail(m, "Receive sample allocation failed");
          return;
        }
        m->receives[m->receive_count++] =
            (document_receive_sample){app_us(), tick, tick - previous, start};
      }
    }
    m->merge_us += app_us() - start;
  }
}
bool document_open(document_model *m, const char *directory, const document_config *cfg,
                   void *identity) {
  memset(m, 0, sizeof(*m));
  m->calibration.fd = -1;
  m->config = *cfg;
  m->online = true;
  if (!app_string(&m->directory, directory, strlen(directory)))
    return false;
  const uint8_t *key = df_identity_public(identity);
  if (!key)
    return fail(m, "Missing owner identity");
  for (size_t i = 0; i < 32; i++)
    snprintf(m->actor + i * 2, 3, "%02x", key[i]);
  app_buffer path = {0};
  bool ok = app_format(&path, "%s/document-yrs-v2-%s/own", directory, cfg->room) &&
            ui_open(&m->own, app_cstr(&path), m->actor, 2, (size_t)1 << 28) &&
            app_format(&path, "%s/document-yrs-v2-%s/peer-%s", directory, cfg->room, cfg->peer) &&
            ui_open(&m->peer, app_cstr(&path), cfg->peer, 2, (size_t)1 << 28);
  app_buffer_free(&path);
  if (!ok)
    return fail(m, "Document store open failed");
  df_match state = {df_id(1), 35, df_var(0), true}, text = {df_id(1), 34, df_var(0), true};
  m->own_state = ui_subscribe(&m->own, &state, 1, false, NULL, NULL);
  m->peer_state = ui_subscribe(&m->peer, &state, 1, false, remote, m);
  m->text = ui_subscribe(&m->own, &text, 1, false, NULL, NULL);
  if (!m->own_state || !m->peer_state || !m->text)
    return fail(
        m,
        "Document subscription failed"); /* Retire the old benchmark-only acknowledgement fact. */
  df_edit cleanup = {0};
  df_edit_unset(&cleanup, 1, DF_ATTR_DOCUMENT_ACK);
  ok = ui_change(&m->own, &cleanup);
  df_buffer out = {0};
  if (!ok || !restore(m) || !publish_current(m, m->local_tick))
    return false;
  m->connection = df_network_open(cfg->host, cfg->port, &out);
  ok = m->connection && df_network_push(m->connection, m->own.engine, identity, &out) &&
       df_network_follow(m->connection, m->peer.engine, &out);
  if (ok && (cfg->probe_interval_ms || cfg->clock_port))
    ok = df_network_measure(m->connection, (uint64_t)cfg->probe_interval_ms, &out) == 0;
  if (!ok) {
    snprintf(m->error, sizeof(m->error), "%.*s", (int)minimum(out.size, 511),
             (const char *)out.data);
    df_free(out.data);
    return false;
  }
  df_free(out.data);
  if (cfg->clock_port && !clock_probe_open(&m->calibration, cfg->clock_host, cfg->clock_port))
    return fail(m, "Clock calibration unavailable");
  if (cfg->role == 0 && cfg->seed && !m->local_tick && !m->peer_tick) {
    char *seed = malloc((size_t)cfg->seed + 1);
    if (!seed)
      return fail(m, "Seed allocation failed");
    memset(seed, 'x', (size_t)cfg->seed);
    seed[cfg->seed] = 0;
    ok = document_replace(m, seed);
    free(seed);
  }
  return ok;
}
void document_close(document_model *m) {
  df_network_close(m->connection);
  clock_probe_close(&m->calibration);
  ui_close(&m->own);
  ui_close(&m->peer);
  sequence_close(&m->doc);
  app_buffer_free(&m->directory);
  free(m->edits);
  free(m->receives);
  memset(m, 0, sizeof(*m));
  m->calibration.fd = -1;
}
bool document_text(document_model *m, df_buffer *out) {
  return m->text && ui_result_bytes(m->text->result, 1, 34, out);
}
static bool change(document_model *m, document_sequence *draft, const char *next) {
  int64_t start = app_us();
  bool modified = false;
  app_buffer before = {0}, state = {0};
  if (draft && !sequence_state(draft, &before))
    return fail(m, "Draft snapshot failed");
  bool ok = sequence_replace(draft ? draft : &m->doc, next, &modified);
  if (ok && modified && draft)
    ok =
        sequence_state(draft, &state) && sequence_merge(&m->doc, state.data, state.size, &modified);
  if (!ok) {
    fail(m, "Document edit failed");
    goto rollback;
  }
  if (!modified) {
    app_buffer_free(&before);
    app_buffer_free(&state);
    return true;
  }
  int64_t crdt_done = app_us();
  if (!sequence_state(&m->doc, &state))
    goto rollback;
  int64_t encoded = app_us();
  uint64_t tick = m->local_tick + 1;
  if (!app_reserve(&m->edits, &m->edit_capacity, m->edit_count + 1, sizeof(*m->edits)) ||
      !publish(m, tick, &state))
    goto rollback;
  int64_t committed = app_us();
  m->edits[m->edit_count++] = (document_edit_sample){start,
                                                     committed - start,
                                                     tick,
                                                     1,
                                                     crdt_done - start,
                                                     encoded - crdt_done,
                                                     committed - encoded,
                                                     0,
                                                     df_revision(m->own.engine)};
  app_buffer_free(&before);
  app_buffer_free(&state);
  return true;
rollback:
  restore(m);
  if (draft) {
    sequence_close(draft);
    if (sequence_open(draft))
      sequence_merge(draft, before.data, before.size, &modified);
  }
  app_buffer_free(&before);
  app_buffer_free(&state);
  return false;
}
bool document_replace(document_model *m, const char *next) { return change(m, NULL, next); }
bool document_replace_draft(document_model *m, document_sequence *draft, const char *next) {
  return change(m, draft, next);
}
void document_online(document_model *m, bool value) {
  m->online = value;
  df_network_online(m->connection, value);
}
const char *document_error(const document_model *m) {
  if (m->error[0])
    return m->error;
  const char *network_error = df_network_error(m->connection);
  return network_error ? network_error : "";
}
bool document_pump(document_model *m) {
  clock_probe_pump(&m->calibration);
  int64_t start = app_us();
  df_network_pump(m->connection, 1500);
  m->pump_us += app_us() - start;
  if (!m->config.start_ms || m->done)
    return true;
  int64_t elapsed = app_wall_ms() - m->config.start_ms;
  if (elapsed < 0)
    return true;
  if (m->config.scenario == 3) {
    if (elapsed >= 2000 && !m->went_offline) {
      document_online(m, false);
      m->went_offline = true;
    }
    if (elapsed >= 7000 && !m->came_online) {
      document_online(m, true);
      m->came_online = true;
    }
  }
  if (elapsed < 10000 && (m->config.scenario >= 2 || m->config.role == 0) &&
      m->action_index <= elapsed / 150) {
    df_buffer text;
    if (!document_text(m, &text))
      return false;
    app_buffer offsets = {0}, next = {0};
    if (!utf8_offsets((const char *)text.data, text.size, &offsets)) {
      app_buffer_free(&offsets);
      return false;
    }
    size_t count = offsets.size / sizeof(size_t) - 1;
    size_t *positions = (size_t *)offsets.data;
    bool ok;
    if (m->action_index % 7 == 6 && count)
      ok = app_append(&next, text.data, positions[count - 1]);
    else {
      size_t middle = positions[count / 2];
      ok = app_append(&next, text.data, middle) &&
           app_append(&next, m->config.role ? "B" : "a", 1) &&
           app_append(&next, text.data + middle, text.size - middle);
    }
    if (ok)
      ok = document_replace(m, app_cstr(&next));
    app_buffer_free(&offsets);
    app_buffer_free(&next);
    if (!ok)
      return false;
    m->action_index++;
  }
  if (elapsed >= m->config.duration_ms)
    m->done = true;
  return true;
}
bool document_save(document_model *m, void (*extra)(void *, FILE *), void *ctx) {
  app_buffer path = {0}, final = {0}, state = {0};
  df_buffer text;
  if (!document_text(m, &text) || !sequence_state(&m->doc, &state) ||
      !app_format(&path, "%s/document-report.json.tmp", app_cstr(&m->directory)) ||
      !app_format(&final, "%s/document-report.json", app_cstr(&m->directory))) {
    app_buffer_free(&path);
    app_buffer_free(&final);
    app_buffer_free(&state);
    return false;
  }
  FILE *f = fopen(app_cstr(&path), "w");
  bool ok = false;
  if (!f)
    goto done;
  const df_network_stats *s = df_network_statistics(m->connection);
  fputs("{\"implementation\":\"yrs-0.24.0-state-fact-v2\",\"edit_component_names\":[\"crdt\","
        "\"encode_state\",\"commit_publish\",\"reserved\"],\"room\":",
        f);
  app_json(f, m->config.room, strlen(m->config.room));
  fprintf(f, ",\"role\":%d,\"scenario\":%d,\"seed\":%d,\"text\":", m->config.role,
          m->config.scenario, m->config.seed);
  app_json(f, (const char *)text.data, text.size);
  fprintf(f,
          ",\"operations\":%llu,\"state_bytes\":%zu,\"ui_facts\":%zu,\"connected\":%d,\"settled\":%"
          "d,\"error\":",
          (unsigned long long)(m->local_tick + m->peer_tick), state.size,
          df_view_size(m->text->result), df_network_connected(m->connection),
          df_network_settled(m->connection));
  const char *error = document_error(m);
  app_json(f, error, strlen(error));
  fputs(",\"network\":{", f);
#define NET(n) fprintf(f, "\"" #n "\":%llu,", (unsigned long long)s->n)
  NET(sent);
  NET(received);
  NET(requests);
  NET(exchanges);
  NET(restarts);
  NET(connections);
  NET(signed_batches);
  NET(reconcile_us);
  NET(sign_us);
  NET(apply_us);
  NET(request_us);
  NET(request_count);
#undef NET
  fprintf(f, "\"merge_us\":%lld,\"pump_us\":%lld},\"edits\":[", (long long)m->merge_us,
          (long long)m->pump_us);
  for (size_t i = 0; i < m->edit_count; i++) {
    document_edit_sample e = m->edits[i];
    fprintf(f, "%s[%lld,%lld,%llu,%llu,%lld,%lld,%lld,%lld,%llu]", i ? "," : "", (long long)e.when,
            (long long)e.edit_us, (unsigned long long)e.tick, (unsigned long long)e.operations,
            (long long)e.crdt_us, (long long)e.encode_us, (long long)e.commit_us,
            (long long)e.reserved_us, (unsigned long long)e.revision);
  }
  fputs("],\"receives\":[", f);
  for (size_t i = 0; i < m->receive_count; i++) {
    document_receive_sample e = m->receives[i];
    fprintf(f, "%s[%lld,%llu,%llu,%lld]", i ? "," : "", (long long)e.when,
            (unsigned long long)e.tick, (unsigned long long)e.operations, (long long)e.received);
  }
  fprintf(f,
          "],\"optimistic_sync\":{\"uploads\":%llu,\"publications\":%llu,\"fallbacks\":%llu},"
          "\"request_timings\":[",
          (unsigned long long)s->optimistic_uploads, (unsigned long long)s->optimistic_publications,
          (unsigned long long)s->optimistic_fallbacks);
  size_t count = 0;
  const df_request_timing *samples = df_network_samples(m->connection, &count);
  for (size_t i = 0; i < count; i++) {
    df_request_timing t = samples[i];
    fprintf(f, "%s[%u,%u,%u,%llu,%llu,%llu,%llu,%llu]", i ? "," : "", t.request, t.channel,
            t.operation, (unsigned long long)t.elapsed_us, (unsigned long long)t.queue_us,
            (unsigned long long)t.snapshot_revision, (unsigned long long)t.enqueued_us,
            (unsigned long long)t.sent_us);
  }
  fputs("],", f);
  clock_probe_json(&m->calibration, f);
  if (extra) {
    fputc(',', f);
    extra(ctx, f);
  }
  fputs("}\n", f);
  ok = !ferror(f);
  if (fclose(f))
    ok = false;
  if (ok)
    ok = rename(app_cstr(&path), app_cstr(&final)) == 0;
done:
  app_buffer_free(&path);
  app_buffer_free(&final);
  app_buffer_free(&state);
  return ok;
}
