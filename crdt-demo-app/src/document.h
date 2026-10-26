#ifndef CRDT_DEMO_DOCUMENT_H
#define CRDT_DEMO_DOCUMENT_H
#include "text_crdt/crdt.h"
#include "datafriend/network.h"
typedef struct document {
  document_sequence sequence;
  df_store *own, *peer;
  df_watch *watch;
  df_network *network;
  df_edit edit;
  ct_bytes state;
  uint64_t revision;
  char error[512];
} document;
/* Zero-init. Identity outlives the document. Empty peer/host allows local use. */
bool document_open(document *, const char *directory, void *identity, const char *peer,
                   const char *host, uint16_t port);
void document_close(document *);
bool document_merge(document *, const void *, size_t);
bool document_edit(document *, document_sequence *displayed, const char *text);
bool document_flush(document *);
#endif
