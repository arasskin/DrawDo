#include "profiling/document_frames.h"
static void collect(document_frames *p) {
  for (size_t i = p->count > 20 ? p->count - 20 : 0; i < p->count; i++)
    presentation_collect(&p->samples[i].stamp);
}
void document_frames_begin(document_frames *p, bool record) {
  collect(p);
  p->active = record;
  if (!record)
    return;
  p->current = (document_frame_sample){0};
  p->current.start = app_now();
  p->current.interval = p->previous ? p->current.start - p->previous : 0;
  p->previous = p->current.start;
  p->cpu_start = app_clock(CLOCK_THREAD_CPUTIME_ID);
  presentation_begin(&p->current.stamp);
}
void document_frames_work(document_frames *p, uint64_t tick) {
  if (p->active) {
    p->current.work = app_now() - p->current.start;
    p->current.peer_tick = tick;
  }
}
void document_frames_track(document_frames *p, const void *raw) {
  if (p->active)
    presentation_track(&p->current.stamp, raw);
}
void document_frames_end(document_frames *p) {
  if (!p->active)
    return;
  p->current.cpu = app_clock(CLOCK_THREAD_CPUTIME_ID) - p->cpu_start;
  p->current.total = app_now() - p->current.start;
  if (app_reserve(&p->samples, &p->capacity, p->count + 1, sizeof(*p->samples)))
    p->samples[p->count++] = p->current;
  else {
    presentation_release(&p->current.stamp);
    p->failed = true;
  }
}
void document_frames_json(void *ctx, FILE *f) {
  document_frames *p = ctx;
  collect(p);
  fprintf(f, "\"frames_complete\":%s,\"frames\":[", p->failed ? "false" : "true");
  for (size_t i = 0; i < p->count; i++) {
    document_frame_sample *s = &p->samples[i];
    fprintf(f, "%s[%lld,%lld,%lld,%lld,%lld,%lld,%llu,%lld]", i ? "," : "", (long long)s->start,
            (long long)s->interval, (long long)s->cpu, (long long)s->total, (long long)s->work,
            (long long)s->stamp.present, (unsigned long long)s->peer_tick,
            (long long)s->steady_offset);
  }
  fputc(']', f);
}
void document_frames_free(document_frames *p) {
  for (size_t i = 0; i < p->count; i++)
    presentation_release(&p->samples[i].stamp);
  free(p->samples);
  memset(p, 0, sizeof(*p));
}
