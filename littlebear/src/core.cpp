#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctype.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>
#include <utility>
#include "main_data_containers/geometric_rank_partitioned_virtual_arena.h"
#include "stack_of_network_message_buffers.h"
#include "slab_arena.h"
#include "size_class_file_heap.h"
#include "storage.h"
#include "parsing.h"
#include "config.h"

enum tag_for_uring_completion {
  server_accepted,
  sent_to_client,
  received_from_client,
};

struct client_connection {
  int file_descriptor;
  tag_for_uring_completion operation;
  stack_of_network_message_buffers::ring_buffer buf;
};

struct connection_buffer_allocator {
  stack_of_network_message_buffers::buffer_stack message_buffer_allocator;
  slab_arena::allocator extra_state_allocator;
};

struct maybe_connection_buffer_allocator {
  connection_buffer_allocator allocator;
  bool has_error;
};

inline maybe_connection_buffer_allocator create_client_two_way_allocator() {
  if (auto [buffer_stack, has_error] = stack_of_network_message_buffers::create(); !has_error) {
    if (auto [arena, has_error] = slab_arena::create(sizeof(client_connection), sizeof(client_connection) * config::MAX_CONCURRENT_CONNECTIONS); !has_error) {
      return {.allocator = {.message_buffer_allocator = buffer_stack, .extra_state_allocator = arena},
              .has_error = false};
    }
  }
  return {{}, true};
}

inline client_connection *reserve(connection_buffer_allocator* conn_allocator) {
  if (char *buf = stack_of_network_message_buffers::reserve(&conn_allocator->message_buffer_allocator); buf != nullptr) {
    if (client_connection *conn = (client_connection *)slab_arena::reserve(&conn_allocator->extra_state_allocator); conn != nullptr) {
      conn->buf = buf;
      return conn;
    }
  }
  return nullptr;
}

inline void release(connection_buffer_allocator* conn_allocator, client_connection *conn) {
  stack_of_network_message_buffers::release(&conn_allocator->message_buffer_allocator, conn->buf);
  slab_arena::release(&conn_allocator->extra_state_allocator, conn);
}


inline int open(int port) {
  if (int fd = socket(AF_INET, SOCK_STREAM, 0); fd >= 0) {
    int val = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (sockaddr *)&addr, sizeof(addr)) >= 0) {
      if (listen(fd, 128) >= 0) {
        return fd;
      } else perror("listen");
    } else perror("bind");
  } else perror("socket");

  return -1;
}

inline int setup_uring(io_uring *ring) {
  io_uring_params params{};
  params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_COOP_TASKRUN | IORING_SETUP_DEFER_TASKRUN;
  if (io_uring_queue_init_params(stack_of_network_message_buffers::NUMBER_OF_BUFFERS, ring, &params) == 0)
    return 0;
  else perror("iouring_queue_init");

  return -1;
}

inline void push_accept(io_uring *ring, sockaddr_in *client_addr, socklen_t *client_len, int listener_fd) {
  io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_accept(sqe, listener_fd, (sockaddr *)client_addr, client_len, 0);
  io_uring_sqe_set_data64(sqe, tag_for_uring_completion::server_accepted);
}

inline void push_network_read(io_uring *ring, client_connection *conn) {
  io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv(sqe, conn->file_descriptor, conn->buf, stack_of_network_message_buffers::BUFFER_SIZE, 0);
  io_uring_sqe_set_data(sqe, conn);
}

inline void push_network_write(io_uring *ring, client_connection *conn, size_t bytes) {
  io_uring_sqe *sqe = io_uring_get_sqe(ring);
  io_uring_prep_send(sqe, conn->file_descriptor, conn->buf, bytes, 0);
  io_uring_sqe_set_data(sqe, conn);
}

enum core_error {
  no_error,
  could_not_open_values_file,
  failed_uring_setup,
  failed_opening_public_port,
  descriptor_too_long,
  unknown_descriptor,
  unknown_debug_descriptor,
  expected_a_number,
  got_an_empty_string,
  failed_at_waiting_for_a_cqe,
  failed_creating_message_buffer_pool,
  could_not_create_connection_allocator,
};

void print_core_error(core_error error) {
  switch (error) {
    case no_error: printf("No error to report :)\n"); break;
    case could_not_open_values_file: printf("could not open values_file\n"); break;
    case failed_uring_setup: printf("failed to setup uring\n"); break;
    case failed_opening_public_port: printf("failed to open public port\n"); break;
    case descriptor_too_long: printf("Message descriptor (the first word in the message) was too long. Didn't read!\n"); break;
    case expected_a_number: printf("Attempted to parse a number but couldn't!\n"); break;
    case got_an_empty_string: printf("Wanted a word but got an empty string instead!\n"); break;
    case unknown_descriptor: printf("Don't know how to recognize the first word you sent! (maybe you meant to turn on debug mode)\n"); break;
    case unknown_debug_descriptor: printf("Don't know how to recognize the first word you sent while in debug mode!\n"); break;
    case failed_at_waiting_for_a_cqe: perror("failed at waiting for a cqe"); break;
    case failed_creating_message_buffer_pool: printf("could not create the buffer pool for our network messages"); break;
    case could_not_create_connection_allocator: printf("could not create the allocator for our client connections"); break;
  }
}

void handle_received(connection_buffer_allocator *allocator, io_uring *ring, int received_bytes, client_connection *conn) {
  core_error error = no_error;
  if (received_bytes > 0) {
    parsing::string_slice buffer_slice = parsing::string_slice{.start = conn->buf, .length = received_bytes};
    parsing::string_slice token;
    token = chop_at(buffer_slice, isspace);
    if (token.length < 15) {

      if (string_slice_equals(token, "add", 3)) {
        parsing::string_slice count_token = chop_next(token, buffer_slice, isspace);
        if (auto [pair_count, has_error] = parsing::ss_to_ull(count_token); !has_error) {
          size_t key_pair_index = 0;
          parsing::string_slice k{};
          parsing::string_slice v = count_token;
          while (key_pair_index < pair_count) {
            if (k = chop_next(v, buffer_slice, isspace); k.length != 0) {
              if (auto [k_as_long, k_has_error] = parsing::ss_to_ull(k); !k_has_error) {
                if (v = chop_next(k, buffer_slice, isspace); v.length != 0) {
                  print_token("add k ",k);
                  print_token("add v ",v);
                  key_pair_index++;
                } else {error = got_an_empty_string; break;}
              } else {error = expected_a_number; break;}
            } else {error = got_an_empty_string; break;}
          }
          if (error == no_error) {
            conn->operation = tag_for_uring_completion::sent_to_client;
            push_network_write(ring, conn, received_bytes);
          }
        } else error = expected_a_number;
      }

      else if (string_slice_equals(token, "retract", 7)) {
        parsing::string_slice count_token = chop_next(token, buffer_slice, isspace);
        if (auto [pair_count, has_error] = parsing::ss_to_ull(count_token); !has_error) {
          size_t key_pair_index = 0;
          parsing::string_slice k{};
          parsing:: string_slice v = count_token;
          while (key_pair_index < pair_count) {
            if (k = chop_next(v, buffer_slice, isspace); k.length != 0) {
              if (auto [k_as_long, k_has_error] = parsing::ss_to_ull(k); !k_has_error) {
                if (v = chop_next(k, buffer_slice, isspace); (v.length != 0)) {
                  print_token("retract k ",k);
                  print_token("retract v ",v);
                  key_pair_index++;
                } else {error = got_an_empty_string; break;}
              } else {error = expected_a_number; break;}
            } else {error = got_an_empty_string; break;}
          }
          if (error == no_error) {
            conn->operation = tag_for_uring_completion::sent_to_client;
            push_network_write(ring, conn, received_bytes);
          }
        } else error = expected_a_number;
      }

      else if (config::DEBUG) {
        if (string_slice_equals(token, "size_class", 10)) {
          parsing::string_slice convert = parsing::chop_next(token, buffer_slice, isspace);
          if (auto [convert_num, has_error] = parsing::ss_to_ull(convert); !has_error) {
            int occupies = size_class_file_heap::next_smallest_slot_size(convert_num);
            int len = snprintf(conn->buf, 13, "%d\n", occupies);
            conn->operation = tag_for_uring_completion::sent_to_client;
            push_network_write(ring, conn, len);
          } else error = expected_a_number;
        }

        else if (string_slice_equals(token, "rank", 4)) {
          parsing::string_slice convert = chop_next(token, buffer_slice, isspace);
          if (auto [convert_num, has_error] = parsing::ss_to_ull(convert); !has_error) {
            int rank = geometric_rank_partioned_virtual_slab_arena::partition(convert_num);
            int len = snprintf(conn->buf, 100, "goes to rank: %d\n", rank);
            conn->operation = tag_for_uring_completion::sent_to_client;
            push_network_write(ring, conn, len);
          } else error = expected_a_number;
        }

        else if (string_slice_equals(token, "alive?", 6)) {
          int len = snprintf(conn->buf, 13, "yes. yay!\n");
          conn->operation = tag_for_uring_completion::sent_to_client;
          push_network_write(ring, conn, len);
        } else error = unknown_debug_descriptor;

      } else error = unknown_descriptor;
    } else error = descriptor_too_long;

    if (error != no_error) {
      if (config::DEBUG) print_core_error(error);
      printf("disconnecting client %d\n", conn->file_descriptor);
      close(conn->file_descriptor);
      release(allocator, conn);
    }
  } else {
    printf("client %d disconnected\n", conn->file_descriptor);
    close(conn->file_descriptor);
    release(allocator, conn);
  }
}

void handle_sent(connection_buffer_allocator *allocator, io_uring *ring, int sent_bytes, client_connection *conn) {
  if (sent_bytes >= 0) {
    conn->operation = tag_for_uring_completion::received_from_client;
    push_network_read(ring, conn);
  } else {
    printf("client %d disconnected\n", conn->file_descriptor);
    close(conn->file_descriptor);
    release(allocator, conn);
  }
}

int main() {
  core_error error = no_error;
  storage::store cache{};
  if (auto result = storage::create(&cache, config::VAUES_FILE_NAME, config::VALUES_FILE_SIZE);
      result.code == storage::error::ok) {
    if (auto [client_connection_allocator, has_error] = create_client_two_way_allocator(); !has_error) {
      struct io_uring ring_on_stack{};
      struct io_uring *ring = &ring_on_stack;
      if (setup_uring(ring) == 0) {
        if (int server_fd = open(config::PUBLIC_PORT); server_fd >= 0) {
          printf("Starting event loop...\n");
          sockaddr_in client_addr{};
          socklen_t client_len = sizeof(client_addr);
          push_accept(ring, &client_addr, &client_len, server_fd);
          io_uring_submit(ring);

          while (true) {
            io_uring_cqe *cqe;
            if (io_uring_wait_cqe(ring, &cqe) == 0) {
              do {uint64_t cqe_data = io_uring_cqe_get_data64(cqe);
                  int cqe_result = cqe->res;
                  io_uring_cqe_seen(ring, cqe);
                  switch (cqe_data) {
                    case tag_for_uring_completion::server_accepted: {
                      int client_conn_fd = cqe_result;
                      if (client_conn_fd >= 0) {
                        printf("new connection %d\n", client_conn_fd);
                        client_connection *conn = reserve(&client_connection_allocator);
                        conn->file_descriptor = client_conn_fd;
                        conn->operation = tag_for_uring_completion::received_from_client;
                        push_network_read(ring, conn);
                      }
                      push_accept(ring, &client_addr, &client_len, server_fd);
                      break;
                    }
                    default: {
                      client_connection *conn = (client_connection *)cqe_data;
                      switch (conn->operation) {
                        case tag_for_uring_completion::received_from_client: handle_received(&client_connection_allocator, ring, cqe_result, conn); break;
                        case tag_for_uring_completion::sent_to_client:       handle_sent(&client_connection_allocator, ring, cqe_result, conn); break;
                        case tag_for_uring_completion::server_accepted:      std::unreachable(); break;
                      }
                    }
                  }
              } while (io_uring_peek_cqe(ring, &cqe) == 0);
            io_uring_submit(ring);
            } else error = failed_at_waiting_for_a_cqe;
          }

        } else error = failed_opening_public_port;
      } else error = failed_uring_setup;
    } else error = could_not_create_connection_allocator;
  } else error = could_not_open_values_file;

  const auto cleanup = storage::destroy(&cache);
  (void)cleanup; // No record I/O is submitted by the prototype request loop yet.
  print_core_error(error);
  return -1;
}
