#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include "config.h"

namespace stack_of_network_message_buffers {

constexpr size_t PAGE_SIZE = 4 * 1024;
constexpr size_t BUFFER_SIZE = 1 * PAGE_SIZE; //buffer size has to be a multiple of page size
constexpr int NUMBER_OF_BUFFERS = config::MAX_CONCURRENT_CONNECTIONS;

using ring_buffer = char *;

inline ring_buffer ring() {
  if (int fd = memfd_create("ring", 0); fd >= 0) {
    if (ftruncate(fd, BUFFER_SIZE) == 0) {
      if (char *base = (char *)mmap(NULL, BUFFER_SIZE * 2, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); base != (void *)-1) {
        if (mmap(base, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != (void *)-1) {
          if (mmap(base + BUFFER_SIZE, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) != (void *)-1) {
            return base;
          } else perror("network_message_buffer mmap second half");
        } else perror("network_message_buffer mmap first half");
      } else perror("network_message_buffer mmap");
    } else perror("network_message_buffer ftruncate");
  } else perror("network_message_buffer memfd_create");

  return nullptr;
}


struct buffer_stack {
  int head;
  ring_buffer array[NUMBER_OF_BUFFERS];
};

inline bool assertions(buffer_stack *stack) {
  assert(stack->head >= 0);
  assert(stack->head < NUMBER_OF_BUFFERS);
  int i = 0;
  while (i <= stack->head) {
    assert(stack->array[i] != nullptr);
    ++i;
  }
  while (i < NUMBER_OF_BUFFERS) {
    assert(stack->array[i] == nullptr);
    ++i;
  }
  return true;
}

struct maybe_buffer_stack {
  buffer_stack stack;
  bool has_error;
};

inline maybe_buffer_stack create() {
  buffer_stack stack;
  stack.head = NUMBER_OF_BUFFERS - 1;
  size_t i = 0;
  while (i < NUMBER_OF_BUFFERS) {
    ring_buffer address = ring(); if (address == nullptr) return {stack, true};
    stack.array[i] = address;
    ++i;
  }
  assert(assertions(&stack));
  return {stack, false};
}

inline ring_buffer reserve(buffer_stack *stack) {
  assert(assertions(stack));
  if (stack->head > 0) {
    ring_buffer address  = stack->array[stack->head];
    stack->array[stack->head] = nullptr;
    stack->head--;
    assert(assertions(stack));
    return address;
  } else {
    return nullptr;
  }
}

inline void release(buffer_stack *stack, ring_buffer address) {
  assert(assertions(stack));
  assert(stack->head < NUMBER_OF_BUFFERS - 1);
  stack->head++;
  stack->array[stack->head] = address;
  assert(assertions(stack));
}

}
