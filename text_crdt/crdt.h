#ifndef TEXT_CRDT_H
#define TEXT_CRDT_H
#include "containers/bytes.h"
#include "libyrs.h"
typedef struct document_sequence {
  YDoc *doc;
  Branch *text_type;
  YSubscription *updates;
  ct_bytes text;
  bool changed;
} document_sequence;
bool sequence_open(document_sequence *);
void sequence_close(document_sequence *);
bool sequence_state(document_sequence *, ct_bytes *);
bool sequence_merge(document_sequence *, const void *, size_t, bool *changed);
bool sequence_replace(document_sequence *, const char *, bool *changed);
bool sequence_fork(document_sequence *, document_sequence *);
bool utf8_offsets(const char *, size_t, ct_bytes *);
#endif
