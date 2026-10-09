#pragma once
#include "storage.h"
#include "protocol.h"
#include <coroutine>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace lb_sync {
// A request's coroutine frame owns every buffer used by its outstanding I/O.
// A disconnected client retains the task until the kernel completion arrives.
class response_task {
 public:
  struct promise_type;
  using handle=std::coroutine_handle<promise_type>;
  struct promise_type {
    storage::request* pending=nullptr;
    std::string response;
    std::exception_ptr error;
    response_task get_return_object() { return response_task(handle::from_promise(*this)); }
    std::suspend_always initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_value(std::string value) { response=std::move(value); }
    void unhandled_exception() noexcept { error=std::current_exception(); }
  };
 private:
  handle h_{};
  explicit response_task(handle h):h_(h) {}
 public:
  response_task()=default;
  response_task(response_task&& other) noexcept:h_(std::exchange(other.h_,{})) {}
  response_task& operator=(response_task&& other) noexcept {
    if(this!=&other) { this->~response_task(); new(this) response_task(std::move(other)); } return *this;
  }
  response_task(const response_task&)=delete;
  ~response_task() {
    if(!h_) return;
    auto* r=pending();
    if(r&&r->owner) {
      // Never free a frame still borrowed by an SQE/CQE.
      if(r->state==storage::phase::submitted) std::terminate();
      storage::cancel(r);
    }
    h_.destroy();
  }
  explicit operator bool() const { return bool(h_); }
  bool done() const { return h_&&h_.done(); }
  storage::request* pending() const { return h_?h_.promise().pending:nullptr; }
  void resume() { h_.promise().pending=nullptr; h_.resume(); }
  std::string result() {
    if(!done()) throw std::logic_error("response not complete");
    if(h_.promise().error) std::rethrow_exception(h_.promise().error);
    return std::move(h_.promise().response);
  }
};
struct file_io {
  storage::request& request;
  bool await_ready() const noexcept { return false; }
  void await_suspend(response_task::handle handle) const noexcept { handle.promise().pending=&request; }
  void await_resume() const {
    if(request.state!=storage::phase::finished||request.outcome.code!=storage::error::ok)
      throw std::runtime_error("Littlebear storage operation failed");
  }
};
inline response_task ready_reply(std::string reply) { co_return std::move(reply); }
inline response_task rejected_response() {
  writer w; w.u8(2); w.u64(0); w.u64(0); w.blob("request rejected"); co_return w.data;
}
}
