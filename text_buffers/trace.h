#ifndef TEXT_BUFFERS_TRACE_H
#define TEXT_BUFFERS_TRACE_H
#include <stdint.h>
/* Optional diagnostic hooks. Only referenced with TEXT_BUFFERS_TRACE defined;
 * the instrumented host supplies these functions. Normal builds have no probes.
 * DRAW means onDraw completion on Android, layoutSubviews completion on Apple.
 * Neither DRAW nor COMMIT asserts that pixels have reached the display. */
enum {
  TEXT_TRACE_RECEIVE = 1,
  TEXT_TRACE_MERGED,
  TEXT_TRACE_REQUEST,
  TEXT_TRACE_NATIVE_BEGIN,
  TEXT_TRACE_NATIVE_SET,
  TEXT_TRACE_DRAW,
  TEXT_TRACE_COMMIT,
  TEXT_TRACE_REJECTED
};
uint64_t text_trace_clock(void);
uint32_t text_trace_tag(const char *);
void text_trace_record(uint32_t tag, unsigned stage, uint64_t ns);
#endif
