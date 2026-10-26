#pragma once
#include <memory>
#include <utility>
namespace lb_sync {
inline void check(storage::result r) {
  if (r.code != storage::error::ok)
    throw std::runtime_error("Littlebear storage operation failed");
}
struct state : lb_service {
  state() { static_cast<lb_service &>(*this) = {}; }
  state(const state &) = delete;
  ~state() { lb_service_destroy(this); }
};
inline lb_namespace name_key(std::string_view name) {
  if (name.size() != 16)
    throw std::invalid_argument("invalid namespace digest");
  return lb_name(reinterpret_cast<const uint8_t *>(name.data()));
}
inline lb_bytes view(std::string_view b) {
  return lb_bytes_make(b.data(), b.size());
}
inline std::string take_buffer(lb_buffer &b) {
  if (b.failed) {
    lb_buffer_free(&b);
    throw std::bad_alloc();
  }
  try {
    std::string out(b.size ? reinterpret_cast<const char *>(b.data) : "", b.size);
    lb_buffer_free(&b);
    return out;
  } catch (...) {
    lb_buffer_free(&b);
    throw;
  }
}
inline void create(state *s, const char *path, uint64_t epoch,
                   uint64_t capacity = uint64_t(1) << 30) {
  check(lb_service_create(s, path, epoch, capacity));
}
inline uint64_t generation(const state *s) {
  return s->generation;
}
inline uint64_t revision(const state *s, const std::string &name) {
  return lb_service_revision(const_cast<state *>(s), name_key(name));
}
inline bool writing(const state *s, const std::string &name) {
  return lb_service_writing(const_cast<state *>(s), name_key(name));
}
inline std::string opening(state *s, const std::string &name, uint8_t status = 0) {
  lb_buffer out{};
  if (!lb_opening(s, name_key(name), status, &out)) {
    lb_buffer_free(&out);
    throw std::bad_alloc();
  }
  return take_buffer(out);
}
inline void perform(storage::request &r) {
  check(lb_perform(&r));
}
class response_task {
  struct dispose {
    void operator()(lb_task *t) const {
      if (t) {
        lb_task_dispose(t);
        delete t;
      }
    }
  };
  std::unique_ptr<lb_task, dispose> task_;

public:
  response_task() = default;
  explicit response_task(lb_task *t) : task_(t) {}
  explicit operator bool() const { return bool(task_); }
  bool done() const { return task_ && task_->phase == lb_task_done; }
  storage::request *pending() const {
    return task_ && !done() && (task_->io.owner || task_->io.state == lb_phase_finished)
               ? &task_->io
               : nullptr;
  }
  void resume() { lb_task_step(task_.get()); }
  std::string result() {
    if (!done())
      throw std::logic_error("response not complete");
    return take_buffer(task_->reply);
  }
};
inline response_task handle_async(state *s, unsigned op, std::string name, bool writable,
                                  uint64_t epoch, uint64_t revision, std::string payload) {
  auto t = std::make_unique<lb_task>();
  lb_task_init(t.get(), s, op, name_key(name), writable, epoch, revision, view(payload));
  return response_task(t.release());
}
inline std::string handle(state *s, unsigned op, const std::string &name, bool writable,
                          uint64_t epoch, uint64_t revision, std::string_view payload) {
  lb_buffer reply{};
  lb_handle(s, op, name_key(name), writable, epoch, revision, view(payload), &reply);
  return take_buffer(reply);
}
} // namespace lb_sync
