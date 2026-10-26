/* Linux transport: one io_uring completion loop for sockets and file storage. */
#define _GNU_SOURCE
#include "../../containers/map.h"
#include "../../datafriend/include/datafriend/schema.h"
#include "auth.h"
#include "storage_uring.h"
#include "sync.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <sys/random.h>
#include <time.h>
enum { LB_MAX_CLIENTS = 8 };
typedef struct client client;
typedef enum {
  op_accept,
  op_receive,
  op_send,
  op_file,
  op_cancel_receive,
  op_cancel_send,
  op_cancel_accept
} operation;
typedef struct completion {
  operation kind;
  client *owner;
} completion;
typedef struct channel {
  uint32_t id;
  uint8_t name[16];
  lb_auth auth;
  uint64_t announced;
  bool subscribed, known, optimistic, needs_reconcile;
} channel;
struct client {
  int fd;
  bool closing, receiving, sending, file_pending, cancelling_receive, cancelling_send,
      cancelled_receive, cancelled_send;
  bool body, task_active;
  uint8_t header[4];
  size_t received, sent;
  lb_buffer input, output;
  lb_task task;
  channel *channels;
  size_t channel_count, channel_capacity, active, next_channel;
  ct_map channel_index;
  uint32_t request_id, channel_id;
  uint8_t op;
  uint64_t generation;
  bool events_dirty;
  completion receive, send, file, cancel_receive, cancel_send;
  uint64_t deadline;
};
typedef struct loop {
  struct io_uring ring;
  int listener;
  bool accepting, cancelling_accept, accept_cancel_requested;
  completion accept, cancel_accept;
  client clients[LB_MAX_CLIENTS];
} loop;
typedef struct options {
  const char *values, *bind;
  int port;
} options;
static volatile sig_atomic_t stopping;
static void stop(int sig) {
  (void)sig;
  stopping = 1;
}
static uint64_t now_ms(void) {
  struct timespec t;
  if (clock_gettime(CLOCK_MONOTONIC, &t))
    abort();
  return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static void init_client(client *c) {
  memset(c, 0, sizeof(*c));
  c->fd = -1;
  c->active = SIZE_MAX;
  c->receive.kind = op_receive;
  c->send.kind = op_send;
  c->file.kind = op_file;
  c->cancel_receive.kind = op_cancel_receive;
  c->cancel_send.kind = op_cancel_send;
  c->receive.owner = c->send.owner = c->file.owner = c->cancel_receive.owner =
      c->cancel_send.owner = c;
}
static void reset(client *c) {
  if (c->receiving || c->sending || c->file_pending || c->cancelling_receive || c->cancelling_send)
    abort();
  lb_task_dispose(&c->task);
  lb_buffer_free(&c->input);
  lb_buffer_free(&c->output);
  free(c->channels);
  ct_map_free(&c->channel_index);
  if (c->fd >= 0)
    close(c->fd);
  init_client(c);
}
static void submit_accept(loop *s) {
  struct io_uring_sqe *sqe;
  if (s->accepting || stopping)
    return;
  sqe = io_uring_get_sqe(&s->ring);
  if (!sqe)
    return;
  io_uring_prep_accept(sqe, s->listener, NULL, NULL, SOCK_CLOEXEC);
  io_uring_sqe_set_data(sqe, &s->accept);
  s->accepting = true;
}
static void cancel_socket(loop *s, bool pending, bool *requested, bool *cancelling,
                          completion *original, completion *tag) {
  struct io_uring_sqe *sqe;
  if (!pending || *requested)
    return;
  sqe = io_uring_get_sqe(&s->ring);
  if (!sqe)
    return;
  io_uring_prep_cancel(sqe, original, 0);
  io_uring_sqe_set_data(sqe, tag);
  *requested = *cancelling = true;
}
static void close_client(loop *s, client *c) {
  c->closing = true;
  cancel_socket(s, c->receiving, &c->cancelled_receive, &c->cancelling_receive, &c->receive,
                &c->cancel_receive);
  cancel_socket(s, c->sending, &c->cancelled_send, &c->cancelling_send, &c->send, &c->cancel_send);
  if (!c->file_pending) {
    lb_task_dispose(&c->task);
    c->task_active = false;
  }
  if (!c->receiving && !c->sending && !c->file_pending && !c->cancelling_receive &&
      !c->cancelling_send)
    reset(c);
}
static bool frame(client *c, uint32_t request, uint32_t id, lb_bytes reply) {
  if (reply.size + 8 > LB_MAX_FRAME)
    return false;
  lb_buffer_clear(&c->output);
  lb_u32(&c->output, (uint32_t)reply.size + 8);
  lb_u32(&c->output, request);
  lb_u32(&c->output, id);
  lb_append(&c->output, reply.data, reply.size);
  c->sent = 0;
  return !c->output.failed;
}
static void dispatch(client *c, lb_service *service) {
  lb_request request;
  lb_buffer reply = CT_ZERO;
  uint8_t name[16];
  lb_auth *proof;
  ct_pair *found;
  bool ready = true;
  uint8_t status;
  c->active = SIZE_MAX;
  c->request_id = c->channel_id = 0;
  c->op = 255;
  c->task_active = true;
  if (c->input.size >= 12) {
    lb_reader r = {lb_bytes_make(c->input.data + 4, c->input.size - 4), false};
    c->request_id = lb_read_u32(&r);
    c->channel_id = lb_read_u32(&r);
  }
  if (!lb_parse(lb_buffer_bytes(&c->input),
                lb_bytes_make(DATAFRIEND_SCHEMA, sizeof(DATAFRIEND_SCHEMA) - 1), &request))
    goto reject;
  c->op = request.op;
  if (request.op == 7) {
    lb_reply_header(&reply, 0, 0, 0);
    goto done;
  }
  if (!lb_auth_namespace(request.public_key, name))
    goto reject;
  found = ct_map_get(&c->channel_index, request.channel);
  if (found)
    c->active = (size_t)found->value;
  else {
    if (!ct_array_reserve_limit(&c->channels, &c->channel_capacity, c->channel_count + 1,
                                sizeof(*c->channels), UINT32_MAX) ||
        !ct_map_put(&c->channel_index, request.channel, c->channel_count))
      goto reject;
    c->active = c->channel_count++;
    memset(&c->channels[c->active], 0, sizeof(*c->channels));
    c->channels[c->active].id = request.channel;
    memcpy(c->channels[c->active].name, name, 16);
  }
  if (memcmp(c->channels[c->active].name, name, 16))
    goto reject;
  proof = &c->channels[c->active].auth;
  if ((request.op == 3 || request.op == 6) && !lb_auth_verify(proof, &request))
    goto reject;
  if (request.op == 6 && c->channels[c->active].needs_reconcile)
    lb_reply_header(&reply, 0, service->epoch, 0);
  else if (request.op == 4 || request.op == 8) {
    if (!lb_opening(service, lb_name(name), 0, &reply))
      goto reject;
  } else if (request.op == 5)
    lb_reply_header(&reply, 0, request.epoch, request.revision);
  else if (request.op <= 1) {
    if (!lb_handle(service, request.op, lb_name(name), false, request.epoch, request.revision,
                   request.payload, &reply) ||
        !reply.size)
      goto reject;
    status = reply.data[0];
    if (request.op == 0 && (status == 0 || status == 3)) {
      c->channels[c->active].needs_reconcile = false;
      memset(proof, 0, sizeof(*proof));
      proof->pushing = request.payload.data[0] == 1;
      if (proof->pushing && status == 3 && !lb_auth_challenge(proof, &reply))
        goto reject;
    } else if (request.op == 1 && status == 0 && proof->pushing && !proof->issued &&
               !lb_exchange_equal(request.payload,
                                  lb_bytes_make(reply.data + 17, reply.size - 17))) {
      if (!lb_auth_challenge(proof, &reply))
        goto reject;
    }
  } else {
    lb_task_init(&c->task, service, request.op, lb_name(name), request.op == 3 || request.op == 6,
                 request.epoch, request.revision, request.payload);
    ready = false;
  }
  goto done;
reject:
  lb_buffer_free(&reply);
  lb_task_reject(&c->task);
  ready = false;
done:
  if (ready)
    lb_task_ready(&c->task, &reply);
  lb_buffer_free(&reply);
  lb_buffer_clear(&c->input);
  c->received = 0;
  c->body = false;
}
static bool finish(client *c, lb_buffer *reply) {
  uint8_t status;
  channel *ch = c->active == SIZE_MAX ? NULL : &c->channels[c->active];
  if (reply->failed || reply->size < 17)
    return false;
  status = reply->data[0] & 0x7f;
  if (ch) {
    if ((c->op == 4 || c->op == 8) && status != 2) {
      ch->subscribed = true;
      ch->optimistic = c->op == 8;
      c->events_dirty = true;
    }
    if (ch->subscribed &&
        (status == 3 || ((c->op == 4 || c->op == 8 || status == 1) && reply->size > 17))) {
      ch->announced = lb_integer(lb_bytes_make(reply->data + 9, 8));
      ch->known = true;
    }
    if (c->op == 5 && status == 0) {
      ct_map_erase(&c->channel_index, ch->id);
      --c->channel_count;
      if (c->active != c->channel_count) {
        *ch = c->channels[c->channel_count];
        ct_map_get(&c->channel_index, ch->id)->value = c->active;
      }
      c->next_channel = c->channel_count ? c->next_channel % c->channel_count : 0;
    }
  }
  if (c->op == 6) {
    if (status == 1) {
      if (ch)
        ch->needs_reconcile = true;
      reply->size = 17;
      reply->data[0] = 6;
      if (!frame(c, 0, c->channel_id, lb_buffer_bytes(reply)))
        return false;
    } else if (status != 0 && !frame(c, 0, c->channel_id, lb_buffer_bytes(reply)))
      return false;
  } else if (!frame(c, c->request_id, c->channel_id, lb_buffer_bytes(reply)))
    return false;
  c->active = SIZE_MAX;
  return true;
}
static bool event(client *c, lb_service *service) {
  size_t i;
  if (!c->events_dirty && c->generation == service->generation)
    return true;
  c->events_dirty = true;
  for (i = 0; i < c->channel_count; ++i) {
    channel *ch = &c->channels[c->next_channel];
    c->next_channel = (c->next_channel + 1) % c->channel_count;
    lb_namespace ns;
    uint64_t revision;
    lb_namespace_entry *entry;
    lb_buffer reply = CT_ZERO;
    bool ok;
    if (!ch->subscribed)
      continue;
    ns = lb_name(ch->name);
    if (lb_service_writing(service, ns))
      continue;
    revision = lb_service_revision(service, ns);
    if (ch->known && ch->announced == revision)
      continue;
    entry = lb_service_entry(service, ns);
    if (ch->optimistic && ch->known && entry && entry->delta.size &&
        entry->delta_before == ch->announced && entry->delta_after == revision) {
      lb_reply_header(&reply, 5, service->epoch, revision);
      lb_append(&reply, entry->delta.data, entry->delta.size);
      ok = !reply.failed;
    } else
      ok = lb_opening(service, ns, 4, &reply);
    if (ok)
      ok = frame(c, 0, ch->id, lb_buffer_bytes(&reply));
    lb_buffer_free(&reply);
    if (!ok)
      return false;
    ch->known = true;
    ch->announced = revision;
    return true;
  }
  c->events_dirty = false;
  c->generation = service->generation;
  return true;
}
static void advance(loop *s, client *c, lb_service *service) {
  struct io_uring_sqe *sqe;
  size_t length;
  void *buffer;
  if (c->fd < 0)
    return;
  if (stopping || now_ms() > c->deadline)
    c->closing = true;
  if (c->closing) {
    close_client(s, c);
    return;
  }
  if (c->file_pending)
    return;
  if (!c->sending && !c->output.size && !c->task_active && c->body && c->received == c->input.size)
    dispatch(c, service);
  while (c->task_active) {
    if (c->task.io.owner && c->task.io.state == lb_phase_ready) {
      if (lb_uring_enqueue(&s->ring, &c->task.io, &c->file))
        c->file_pending = true;
      return;
    }
    if (c->task.io.owner && c->task.io.state != lb_phase_finished)
      abort();
    if (c->task.phase != lb_task_done)
      lb_task_step(&c->task);
    if (c->task.phase == lb_task_done) {
      bool ok = finish(c, &c->task.reply);
      lb_task_dispose(&c->task);
      c->task_active = false;
      if (!ok) {
        close_client(s, c);
        return;
      }
    }
  }
  if (!c->sending && !c->output.size && !event(c, service)) {
    close_client(s, c);
    return;
  }
  if (c->output.size && !c->sending) {
    sqe = io_uring_get_sqe(&s->ring);
    if (sqe) {
      io_uring_prep_send(sqe, c->fd, c->output.data + c->sent, c->output.size - c->sent,
                         MSG_NOSIGNAL);
      io_uring_sqe_set_data(sqe, &c->send);
      c->sending = true;
    }
  }
  length = c->body ? c->input.size : sizeof(c->header);
  if (!c->receiving && c->received < length) {
    sqe = io_uring_get_sqe(&s->ring);
    if (sqe) {
      buffer = c->body ? (void *)(c->input.data + c->received) : (void *)(c->header + c->received);
      io_uring_prep_recv(sqe, c->fd, buffer, length - c->received, 0);
      io_uring_sqe_set_data(sqe, &c->receive);
      c->receiving = true;
    }
  }
}
static void complete(loop *s, completion *tag, int result) {
  client *c;
  size_t i;
  if (tag->kind == op_accept) {
    int no_delay = 1;
    s->accepting = false;
    if (result < 0)
      return;
    if (setsockopt(result, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay))) {
      close(result);
      return;
    }
    if (!stopping)
      for (i = 0; i < LB_MAX_CLIENTS; ++i)
        if (s->clients[i].fd < 0) {
          s->clients[i].fd = result;
          s->clients[i].deadline = now_ms() + 30000;
          return;
        }
    close(result);
    return;
  }
  if (tag->kind == op_cancel_accept) {
    s->cancelling_accept = false;
    return;
  }
  c = tag->owner;
  if (tag->kind == op_cancel_receive) {
    c->cancelling_receive = false;
    return;
  }
  if (tag->kind == op_cancel_send) {
    c->cancelling_send = false;
    return;
  }
  if (tag->kind == op_file) {
    c->file_pending = false;
    lb_store_complete_io(&c->task.io, result);
    return;
  }
  if (tag->kind == op_receive)
    c->receiving = false;
  else
    c->sending = false;
  if (c->closing)
    return;
  if (result == -EINTR || result == -EAGAIN)
    return;
  if (result <= 0) {
    c->closing = true;
    return;
  }
  c->deadline = now_ms() + 30000;
  if (tag->kind == op_send) {
    c->sent += (size_t)result;
    if (c->sent == c->output.size) {
      lb_buffer_clear(&c->output);
      c->sent = 0;
    }
    return;
  }
  c->received += (size_t)result;
  if (!c->body && c->received == sizeof(c->header)) {
    uint32_t length = 0;
    for (i = 0; i < sizeof(c->header); ++i)
      length = (length << 8) | c->header[i];
    if (length < 13 || length > LB_MAX_FRAME || !lb_buffer_reserve(&c->input, length)) {
      c->closing = true;
      return;
    }
    c->input.size = length;
    c->received = 0;
    c->body = true;
  }
}
static void run(loop *s, lb_service *service) {
  for (;;) {
    bool clients;
    uint64_t generation;
    int submitted, waited;
    struct io_uring_cqe *cqe = NULL;
    struct __kernel_timespec timeout = {0, 100000000};
    if (stopping && s->accepting && !s->accept_cancel_requested) {
      struct io_uring_sqe *sqe = io_uring_get_sqe(&s->ring);
      if (sqe) {
        io_uring_prep_cancel(sqe, &s->accept, 0);
        io_uring_sqe_set_data(sqe, &s->cancel_accept);
        s->cancelling_accept = s->accept_cancel_requested = true;
      }
    }
    submit_accept(s);
    do {
      size_t i;
      generation = service->generation;
      clients = false;
      for (i = 0; i < LB_MAX_CLIENTS; ++i) {
        advance(s, &s->clients[i], service);
        clients |= s->clients[i].fd >= 0;
      }
    } while (generation != service->generation);
    if (stopping && !clients && !s->accepting && !s->cancelling_accept)
      return;
    submitted = io_uring_submit(&s->ring);
    if (submitted < 0 && submitted != -EINTR) {
      fprintf(stderr, "io_uring submission failed: %d\n", submitted);
      _Exit(2);
    }
    waited = io_uring_wait_cqe_timeout(&s->ring, &cqe, &timeout);
    if (waited == -ETIME || waited == -EINTR)
      continue;
    if (waited < 0) {
      fprintf(stderr, "io_uring wait failed: %d\n", waited);
      _Exit(2);
    }
    do {
      completion *tag = (completion *)io_uring_cqe_get_data(cqe);
      int result = cqe->res;
      io_uring_cqe_seen(&s->ring, cqe);
      if (tag)
        complete(s, tag, result);
    } while (!io_uring_peek_cqe(&s->ring, &cqe));
  }
}
static bool parse_options(int argc, char **argv, options *out) {
  int i;
  out->values = NULL;
  out->bind = "127.0.0.1";
  out->port = 7778;
  for (i = 1; i < argc; i += 2) {
    if (i + 1 == argc)
      return false;
    if (!strcmp(argv[i], "--values"))
      out->values = argv[i + 1];
    else if (!strcmp(argv[i], "--bind"))
      out->bind = argv[i + 1];
    else if (!strcmp(argv[i], "--port")) {
      char *end = NULL;
      long port;
      errno = 0;
      port = strtol(argv[i + 1], &end, 10);
      if (errno || *end || end == argv[i + 1] || port < 0 || port > 65535)
        return false;
      out->port = (int)port;
    } else
      return false;
  }
  return out->values != NULL;
}
static bool boot_nonce(uint64_t *nonce) {
  size_t done = 0;
  while (done < sizeof(*nonce)) {
    ssize_t n = getrandom((char *)nonce + done, sizeof(*nonce) - done, 0);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return false;
    done += (size_t)n;
  }
  return true;
}
int main(int argc, char **argv) {
  options args;
  lb_service service = CT_ZERO;
  loop s = CT_ZERO;
  struct io_uring_params params = CT_ZERO;
  struct sockaddr_in address = CT_ZERO;
  socklen_t size = sizeof(address);
  uint64_t nonce;
  size_t i;
  int reuse = 1, result = 1;
  bool ring_open = false, service_open = false;
  s.listener = -1;
  s.accept.kind = op_accept;
  s.cancel_accept.kind = op_cancel_accept;
  for (i = 0; i < LB_MAX_CLIENTS; ++i)
    init_client(&s.clients[i]);
  if (!parse_options(argc, argv, &args)) {
    fprintf(stderr, "expected --values path [--bind address] [--port number]\n");
    return 1;
  }
  if (sodium_init() < 0 || !boot_nonce(&nonce)) {
    fprintf(stderr, "cryptography initialization failed\n");
    goto done;
  }
  if (lb_service_create(&service, args.values, nonce, UINT64_C(1) << 30).code != lb_error_ok) {
    fprintf(stderr, "cache creation failed\n");
    goto done;
  }
  service_open = true;
  params.flags =
      IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN;
  if (io_uring_queue_init_params(64, &s.ring, &params) < 0) {
    fprintf(stderr, "io_uring setup failed\n");
    goto done;
  }
  ring_open = true;
  s.listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s.listener < 0) {
    perror("socket");
    goto done;
  }
  setsockopt(s.listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  address.sin_family = AF_INET;
  address.sin_port = htons((uint16_t)args.port);
  if (inet_pton(AF_INET, args.bind, &address.sin_addr) != 1 ||
      bind(s.listener, (struct sockaddr *)&address, sizeof(address)) || listen(s.listener, 16)) {
    perror("listen");
    goto done;
  }
  getsockname(s.listener, (struct sockaddr *)&address, &size);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, stop);
  signal(SIGINT, stop);
  printf("Littlebear TCP listening %s:%u\n", args.bind, (unsigned)ntohs(address.sin_port));
  fflush(stdout);
  run(&s, &service);
  result = 0;
done:
  if (s.listener >= 0)
    close(s.listener);
  if (ring_open)
    io_uring_queue_exit(&s.ring);
  if (service_open)
    lb_service_destroy(&service);
  return result;
}
