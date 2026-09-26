#include "check.h"
#include "storage.h"
#ifdef __linux__
#include "storage_uring.h"
#endif
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <sys/stat.h>

namespace storage_tests {
namespace st = storage;
namespace tr = augmented_tree;
const st::namespace_id A{1, 2}, B{3, 4};
struct fixture { st::store s{}; char path[64] = "/tmp/littlebear-storage-XXXXXX"; };
void create(fixture *f, uint64_t limit = 1024 * 1024, size_t pending = 64) {
  int fd = mkstemp(f->path);
  CHECK(fd >= 0);
  close(fd);
  CHECK(st::create(&f->s, f->path, limit, pending, 1, 4096).code == st::error::ok);
}
void destroy(fixture *f) {
  CHECK(st::destroy(&f->s).code == st::error::ok);
  CHECK(unlink(f->path) == 0);
}
uint64_t hash(const std::string &bytes) {
  uint64_t h = UINT64_C(14695981039346656037);
  for (unsigned char b : bytes) h = (h ^ b) * UINT64_C(1099511628211);
  return h;
}
st::result drive(st::request *r, size_t chunk = SIZE_MAX) {
  st::result outcome;
  do {
    st::io_operation op{};
    CHECK(st::start_io(r, &op).code == st::error::ok);
    const size_t length = std::min(size_t(op.length), chunk);
    const ssize_t bytes = op.writing ? pwrite(op.fd, op.buffer, length, op.offset)
                                    : pread(op.fd, op.buffer, length, op.offset);
    outcome = st::complete_io(r, bytes < 0 ? -errno : int(bytes));
  } while (outcome.code == st::error::more_io);
  CHECK(r->owner == nullptr && r->state == st::phase::finished);
  return outcome;
}
void put(st::store *s, st::namespace_id ns, uint64_t key, const std::string &bytes, size_t chunk = SIZE_MAX) {
  st::request r{};
  CHECK(st::prepare_write(s, &r, ns, key, bytes.data(), bytes.size(), hash(bytes)).code == st::error::ok);
  CHECK(drive(&r, chunk).code == st::error::ok);
}
std::string get(st::store *s, st::namespace_id ns, uint64_t key, size_t chunk = SIZE_MAX) {
  char bytes[UINT16_MAX];
  st::request r{};
  CHECK(st::prepare_read(s, &r, ns, key, bytes, sizeof(bytes)).code == st::error::ok);
  CHECK(drive(&r, chunk).code == st::error::ok);
  return {bytes, r.transferred};
}
uint64_t offset(st::store *s, st::namespace_id ns, uint64_t key) {
  return tr::vpointer_get_offset(tr::get(hash_table::get(&s->namespaces, ns), key));
}
void lifecycle() {
  fixture f; create(&f);
  st::request r{};
  char buffer[100]{};
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::absent);
  CHECK(st::prepare_read(&f.s, &r, A, 1, buffer, sizeof(buffer)).code == st::error::namespace_missing);
  CHECK(st::prepare_write(&f.s, &r, A, 1, "a", 1, 1).code == st::error::namespace_missing);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::absent);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::empty);
  CHECK(st::prepare_read(&f.s, &r, A, 1, buffer, sizeof(buffer)).code == st::error::record_missing);
  for (size_t length : {size_t(0), size_t(65536), SIZE_MAX})
    CHECK(st::prepare_write(&f.s, &r, A, 1, buffer, length, 1).code == st::error::invalid_argument);
  const std::string binary("a\0b\n\xff", 5);
  put(&f.s, A, 0, binary, 2);
  CHECK(offset(&f.s, A, 0) == 0);
  CHECK(get(&f.s, A, 0, 1) == binary);
  CHECK(st::prepare_read(&f.s, &r, A, 0, buffer, 4).code == st::error::buffer_too_small);
  CHECK(st::prepare_read(&f.s, &r, A, 0, nullptr, 5).code == st::error::invalid_argument);
  CHECK(st::create_namespace(&f.s, B).code == st::error::ok);
  put(&f.s, B, 0, "different");
  put(&f.s, A, UINT64_MAX, std::string(UINT16_MAX, '\x9e'), 997);
  CHECK(get(&f.s, A, UINT64_MAX, 500) == std::string(UINT16_MAX, '\x9e'));
  CHECK(st::clear_namespace(&f.s, A, 1).code == st::error::more_work);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::populated);
  CHECK(st::clear_namespace(&f.s, A, 1).code == st::error::ok);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::empty);
  CHECK(get(&f.s, B, 0) == "different");
  CHECK(st::remove_namespace(&f.s, A).code == st::error::ok);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::absent);
  CHECK(st::remove_namespace(&f.s, A).code == st::error::namespace_missing);
  CHECK(st::erase_record(&f.s, B, 0).code == st::error::ok);
  CHECK(st::lookup_namespace(&f.s, B) == st::presence::empty);
  destroy(&f);
}
void pending_and_replacement() {
  fixture f; create(&f, 1024 * 1024, 2);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  put(&f.s, A, 10, "old");
  const uint64_t old_offset = offset(&f.s, A, 10);
  const auto old_value = tr::get(hash_table::get(&f.s.namespaces, A), 10);
  st::request write{}, read{}, second{};
  char bytes[20];
  CHECK(st::prepare_write(&f.s, &write, A, 10, "new", 3, hash("new")).code == st::error::ok);
  CHECK(tr::get(hash_table::get(&f.s.namespaces, A), 10) == old_value);
  CHECK(st::prepare_read(&f.s, &read, A, 10, bytes, sizeof(bytes)).code == st::error::busy);
  CHECK(st::clear_namespace(&f.s, A).code == st::error::busy);
  CHECK(st::remove_namespace(&f.s, A).code == st::error::busy);
  CHECK(st::destroy(&f.s).code == st::error::busy);
  const auto before_growth = f.s.namespaces.bucket_count;
  for (uint64_t i = 100; i < 300; ++i)
    CHECK(st::create_namespace(&f.s, {i, i * 3}).code == st::error::ok);
  CHECK(f.s.namespaces.bucket_count > before_growth);
  CHECK(drive(&write, 1).code == st::error::ok);
  CHECK(get(&f.s, A, 10) == "new");
  put(&f.s, A, 11, "reuse");
  CHECK(offset(&f.s, A, 11) == old_offset);
  CHECK(st::prepare_read(&f.s, &read, A, 10, bytes, sizeof(bytes)).code == st::error::ok);
  char other[20];
  CHECK(st::prepare_read(&f.s, &second, A, 11, other, sizeof(other)).code == st::error::ok);
  CHECK(st::prepare_read(&f.s, &write, A, 10, other, sizeof(other)).code == st::error::busy);
  CHECK(st::erase_record(&f.s, A, 10).code == st::error::busy);
  CHECK(st::prepare_write(&f.s, &write, A, 10, "bad", 3, 0).code == st::error::busy);
  CHECK(st::cancel(&second).code == st::error::cancelled);
  st::io_operation op{};
  CHECK(st::start_io(&read, &op).code == st::error::ok);
  CHECK(st::cancel(&read).code == st::error::busy);
  CHECK(st::erase_record(&f.s, A, 10).code == st::error::busy);
  CHECK(st::complete_io(&read, int(pread(op.fd, op.buffer, op.length, op.offset))).code == st::error::ok);
  CHECK(std::string(bytes, read.transferred) == "new");
  CHECK(st::complete_io(&read, 3).code == st::error::invalid_state);
  CHECK(f.s.pending_count == 0);
  destroy(&f);
}
void failures_and_reuse() {
  fixture f; create(&f, 64);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  put(&f.s, A, 1, "old");
  {
    st::request partial{};
    CHECK(st::prepare_write(&f.s, &partial, A, 1, "new", 3, 99).code == st::error::ok);
    st::io_operation op{};
    CHECK(st::start_io(&partial, &op).code == st::error::ok);
    CHECK(pwrite(op.fd, op.buffer, 1, op.offset) == 1);
    CHECK(st::complete_io(&partial, 1).code == st::error::more_io);
    CHECK(st::start_io(&partial, &op).code == st::error::ok);
    CHECK(op.length == 2);
    CHECK(st::complete_io(&partial, -EIO).code == st::error::io_failure);
    CHECK(get(&f.s, A, 1) == "old");
  }
  for (int completion : {-EIO, -ENOSPC, -ECANCELED, 0, 99, INT_MIN}) {
    st::request r{};
    CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, 99).code == st::error::ok);
    st::io_operation op{};
    CHECK(st::start_io(&r, &op).code == st::error::ok);
    CHECK(st::complete_io(&r, completion).code != st::error::ok);
    CHECK(get(&f.s, A, 1) == "old");
    CHECK(f.s.values.file_allocator.high_watermark == 64);
  }
  st::request r{};
  CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, 99).code == st::error::ok);
  const uint64_t rollback_offset = tr::vpointer_get_offset(r.value);
  CHECK(st::cancel(&r).code == st::error::cancelled);
  CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, hash("new")).code == st::error::ok);
  CHECK(tr::vpointer_get_offset(r.value) == rollback_offset);
  st::io_operation op{};
  CHECK(st::start_io(&r, &op).code == st::error::ok);
  CHECK(st::complete_io(&r, -EINTR).code == st::error::more_io);
  CHECK(st::start_io(&r, &op).code == st::error::ok);
  CHECK(st::complete_io(&r, -EAGAIN).code == st::error::more_io);
  CHECK(drive(&r, 1).code == st::error::ok);
  CHECK(get(&f.s, A, 1) == "new");
  put(&f.s, A, 2, "full");
  CHECK(st::prepare_write(&f.s, &r, A, 1, "newer", 5, 0).code == st::error::capacity);
  CHECK(get(&f.s, A, 1) == "new");
  CHECK(st::erase_record(&f.s, A, 2).code == st::error::ok);
  CHECK(st::prepare_write(&f.s, &r, A, 3, "fail index", 10, 0).code == st::error::ok);
  // Exhaust every node partition only at commit, after file allocation succeeds.
  uint32_t saved[32];
  for (uint8_t rank = 0; rank < 32; ++rank) {
    saved[rank] = f.s.nodes.by_rank[rank].high_watermark;
    f.s.nodes.by_rank[rank].high_watermark = st::arena::partition_end(rank);
    // Deleting the previous record left free nodes; save and hide these below.
  }
  st::arena::slab heads[32];
  for (size_t rank = 0; rank < 32; ++rank) {
    heads[rank] = f.s.nodes.by_rank[rank].head;
    f.s.nodes.by_rank[rank].head.next_index = st::arena::INVALID_INDEX;
  }
  CHECK(drive(&r).code == st::error::no_memory);
  // Existing records can still be replaced with every node partition exhausted.
  CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, hash("new")).code == st::error::ok);
  CHECK(drive(&r).code == st::error::ok);
  for (size_t rank = 0; rank < 32; ++rank) {
    f.s.nodes.by_rank[rank].high_watermark = saved[rank];
    f.s.nodes.by_rank[rank].head = heads[rank];
  }
  CHECK(get(&f.s, A, 1) == "new");
  put(&f.s, A, 3, "retry");
  CHECK(get(&f.s, A, 3) == "retry");
  // Real EOF must fail instead of looping forever or reporting a full value.
  CHECK(ftruncate(f.s.values.file_descriptor, 0) == 0);
  char buffer[10];
  CHECK(st::prepare_read(&f.s, &r, A, 1, buffer, sizeof(buffer)).code == st::error::ok);
  CHECK(drive(&r).code == st::error::io_failure);
  destroy(&f);
}
void metadata_failure() {
  fixture f; create(&f);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  put(&f.s, A, 1, "old");
  auto &metadata = f.s.values.file_allocator.slab_allocator;
  const auto saved = metadata;
  metadata.high_watermark_index = metadata.size = INT_MAX;
  metadata.free_slab_head.next_index = -1;
  CHECK(st::erase_record(&f.s, A, 1).code == st::error::no_memory);
  st::request r{};
  CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, 0).code == st::error::no_memory);
  CHECK(st::clear_namespace(&f.s, A).code == st::error::no_memory);
  CHECK(get(&f.s, A, 1) == "old");
  metadata = saved;
  // Tickets acquired before submission make failed-I/O cleanup allocation-free.
  CHECK(st::prepare_write(&f.s, &r, A, 1, "new", 3, 0).code == st::error::ok);
  const int saved_size = metadata.size;
  const int saved_high = metadata.high_watermark_index;
  metadata.high_watermark_index = metadata.size = INT_MAX;
  st::io_operation op{};
  CHECK(st::start_io(&r, &op).code == st::error::ok);
  CHECK(st::complete_io(&r, -ENOSPC).code == st::error::io_failure);
  metadata.size = saved_size;
  metadata.high_watermark_index = saved_high;
  CHECK(get(&f.s, A, 1) == "old");
  put(&f.s, A, 1, "retry");
  destroy(&f);
}
void reset_and_lock() {
  fixture f; create(&f);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  put(&f.s, A, 1, "cached");
  st::store other{};
  CHECK(st::create(&other, f.path, 1024).code == st::error::io_failure);
  CHECK(get(&f.s, A, 1) == "cached");
  CHECK(st::destroy(&other).code == st::error::ok);
  CHECK(st::create(&other, f.path, 0).code == st::error::invalid_argument);
  CHECK(st::create(&other, f.path, six_byte_mask + 2).code == st::error::invalid_argument);
  CHECK(st::destroy(&f.s).code == st::error::ok);
  CHECK(st::create(&f.s, f.path, 1024).code == st::error::ok);
  struct stat info{};
  CHECK(fstat(f.s.values.file_descriptor, &info) == 0 && info.st_size == 0);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::absent);
  CHECK(f.s.values.file_allocator.high_watermark == 0);
  destroy(&f);
}
void concurrent_records() {
  fixture f; create(&f);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  st::request writes[8]{};
  for (size_t i = 0; i < 8; ++i)
    CHECK(st::prepare_write(&f.s, &writes[i], A, i, "batch", 5, hash("batch")).code == st::error::ok);
  CHECK(st::clear_namespace(&f.s, A).code == st::error::busy);
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::empty);
  for (size_t i = 8; i-- != 0;) CHECK(drive(&writes[i]).code == st::error::ok);
  for (size_t i = 0; i < 8; ++i) CHECK(get(&f.s, A, i) == "batch");
  st::request reads[2]{};
  char buffers[2][5];
  for (size_t i = 0; i < 2; ++i)
    CHECK(st::prepare_read(&f.s, &reads[i], A, 1, buffers[i], 5).code == st::error::ok);
  CHECK(st::erase_record(&f.s, A, 2).code == st::error::ok);
  CHECK(drive(&reads[1]).code == st::error::ok);
  CHECK(st::erase_record(&f.s, A, 1).code == st::error::busy);
  CHECK(drive(&reads[0]).code == st::error::ok);
  CHECK(st::erase_record(&f.s, A, 1).code == st::error::ok);
  destroy(&f);
}
void randomized() {
  fixture f; create(&f, 4 * 1024 * 1024);
  std::map<uint64_t, std::string> expected;
  std::mt19937_64 random(0xCA4E);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  for (size_t step = 0; step < 5000; ++step) {
    const uint64_t key = random() % 128;
    if (random() % 3 == 0) {
      auto erased = st::erase_record(&f.s, A, key);
      CHECK(erased.code == (expected.erase(key) ? st::error::ok : st::error::record_missing));
    } else {
      std::string bytes(1 + random() % 127, char(random()));
      put(&f.s, A, key, bytes);
      expected[key] = bytes;
    }
    uint64_t fingerprint = 0;
    for (auto &[k, bytes] : expected) fingerprint ^= hash(bytes);
    auto *index = hash_table::get(&f.s.namespaces, A);
    CHECK(tr::get_full_fingerprint(index) == fingerprint);
    uint64_t range = 0;
    for (auto &[k, bytes] : expected) if (k >= 32 && k < 96) range ^= hash(bytes);
    CHECK(tr::get_range_fingerprint(index, 32, 96) == range);
    if (step % 97 == 0)
      for (auto &[k, bytes] : expected) CHECK(get(&f.s, A, k) == bytes);
  }
  while (st::remove_namespace(&f.s, A, 7).code == st::error::more_work) {}
  CHECK(st::lookup_namespace(&f.s, A) == st::presence::absent);
  destroy(&f);
}
void uring() {
#ifdef __linux__
  io_uring ring{};
  const int initialized = io_uring_queue_init(2, &ring, 0);
  if (initialized < 0) {
    CHECK(std::getenv("LITTLEBEAR_REQUIRE_URING") == nullptr);
    CHECK(initialized == -EPERM || initialized == -ENOSYS || initialized == -EACCES);
    std::printf("SKIP: kernel io_uring unavailable (%d); completion-state tests still run\n", initialized);
    return;
  }
  fixture f; create(&f);
  CHECK(st::create_namespace(&f.s, A).code == st::error::ok);
  CHECK(st::create_namespace(&f.s, B).code == st::error::ok);
  st::request write{}, second{}, read{};
  CHECK(st::prepare_write(&f.s, &write, A, 1, "uring", 5, 1).code == st::error::ok);
  CHECK(st::prepare_write(&f.s, &second, A, 2, "other", 5, 2).code == st::error::ok);
  // Fill the SQ before enqueueing: request stays ready and owns its buffer.
  for (size_t i = 0; i < 2; ++i) {
    auto *sqe = io_uring_get_sqe(&ring);
    CHECK(sqe != nullptr);
    io_uring_prep_nop(sqe);
  }
  CHECK(!storage_uring::enqueue(&ring, &write));
  CHECK(write.state == st::phase::ready);
  CHECK(io_uring_submit(&ring) == 2);
  for (size_t i = 0; i < 2; ++i) {
    io_uring_cqe *cqe;
    CHECK(io_uring_wait_cqe(&ring, &cqe) == 0);
    CHECK(cqe->res == 0);
    io_uring_cqe_seen(&ring, cqe);
  }
  CHECK(storage_uring::enqueue(&ring, &write));
  CHECK(storage_uring::enqueue(&ring, &second));
  CHECK(!storage_uring::enqueue(&ring, &write));
  CHECK(io_uring_submit(&ring) == 2);
  for (size_t i = 0; i < 2; ++i) {
    io_uring_cqe *cqe;
    CHECK(io_uring_wait_cqe(&ring, &cqe) == 0);
    CHECK(storage_uring::complete(cqe).code == st::error::ok);
    io_uring_cqe_seen(&ring, cqe);
  }
  char buffer[5];
  CHECK(st::prepare_read(&f.s, &read, A, 1, buffer, sizeof(buffer)).code == st::error::ok);
  CHECK(storage_uring::enqueue(&ring, &read));
  CHECK(io_uring_submit(&ring) == 1);
  io_uring_cqe *cqe;
  CHECK(io_uring_wait_cqe(&ring, &cqe) == 0);
  CHECK(storage_uring::complete(cqe).code == st::error::ok);
  io_uring_cqe_seen(&ring, cqe);
  CHECK(std::string(buffer, 5) == "uring");
  CHECK(get(&f.s, A, 2) == "other");
  io_uring_queue_exit(&ring);
  destroy(&f);
  std::puts("PASS: real io_uring batched writes, read, and SQ exhaustion");
#endif
}
}
int main() {
  storage_tests::lifecycle();
  storage_tests::pending_and_replacement();
  storage_tests::failures_and_reuse();
  storage_tests::metadata_failure();
  storage_tests::reset_and_lock();
  storage_tests::concurrent_records();
  storage_tests::randomized();
  storage_tests::uring();
  std::printf("Storage metadata: store=%zu bytes, request=%zu bytes (64 requests=%zu bytes, excluding buffers)\n",
              sizeof(storage::store), sizeof(storage::request), 64 * sizeof(storage::request));
  std::puts("PASS: namespace/record lifecycle, short I/O, rollback, cancellation, pinning, reset, and 5,000 model operations");
}
