#pragma once
#include "storage.h"
#include <liburing.h>

namespace storage_uring {
// false leaves a prepared request ready for a later submission attempt.
inline bool enqueue(io_uring *ring, storage::request *request) {
  if (request->owner == nullptr || request->state != storage::phase::ready) return false;
  io_uring_sqe *sqe = io_uring_get_sqe(ring);
  if (sqe == nullptr) return false;
  storage::io_operation operation{};
  const auto started = storage::start_io(request, &operation);
  (void)started; // State was checked before reserving the SQE; single event loop.
  if (operation.writing)
    io_uring_prep_write(sqe, operation.fd, operation.buffer, operation.length, operation.offset);
  else
    io_uring_prep_read(sqe, operation.fd, operation.buffer, operation.length, operation.offset);
  io_uring_sqe_set_data(sqe, request);
  return true;
}

// Driver must route only storage CQEs here and mark them seen itself. more_io
// requests need re-enqueueing; failed io_uring_submit must not free queued buffers.
inline storage::result complete(const io_uring_cqe *cqe) {
  auto *request = static_cast<storage::request *>(io_uring_cqe_get_data(cqe));
  return storage::complete_io(request, cqe->res);
}
}
