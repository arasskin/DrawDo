#pragma once
namespace storage_uring {
inline bool enqueue(io_uring *ring, storage::request *request, void *tag = nullptr) {
  return lb_uring_enqueue(ring, request, tag);
}
inline storage::result complete(const io_uring_cqe *cqe) {
  return lb_uring_complete(cqe);
}
} // namespace storage_uring
