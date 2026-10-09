#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>

namespace datafriend {
// Only the sync syscall runs here. No LMDB transactions, metadata, indexes or
// published maps are accessed by this thread. The owner keeps the fd alive.
class async_flush {
  using clock = std::chrono::steady_clock;
  std::function<int()> sync_;
  const std::chrono::milliseconds interval_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  uint64_t dirty_=0, synced_=0, attempts_=0;
  int error_=0;
  bool stop_=false, urgent_=false;
  clock::time_point deadline_;
  std::thread worker_;
  void run() {
    std::unique_lock lock(mutex_);
    for(;;) {
      wake_.wait(lock,[&]{ return stop_ || dirty_!=synced_; });
      if(dirty_==synced_ && stop_) return;
      wake_.wait_until(lock,deadline_,[&]{ return stop_ || urgent_; });
      const auto target=dirty_;
      urgent_=false;
      lock.unlock();
      int error;
      try { error=sync_(); } catch(...) { error=EIO; }
      lock.lock();
      ++attempts_; error_=error;
      if(!error) synced_=target; // Writes during sync still need another flush.
      deadline_=clock::now()+interval_;
      wake_.notify_all();
      if(stop_ && (error || dirty_==synced_)) return;
    }
  }
 public:
  explicit async_flush(std::function<int()> sync,
                       std::chrono::milliseconds interval=std::chrono::milliseconds(100))
    :sync_(std::move(sync)),interval_(interval),worker_([this]{run();}) {}
  ~async_flush() { stop(); }
  void written() {
    std::lock_guard lock(mutex_);
    if(dirty_==synced_) deadline_=clock::now()+interval_;
    ++dirty_;
    wake_.notify_all(); // Does not postpone the deadline for an existing batch.
  }
  void check() const {
    std::lock_guard lock(mutex_);
    if(error_) throw std::system_error(error_,std::generic_category(),"Datafriend background flush failed");
  }
  void flush() {
    std::unique_lock lock(mutex_);
    const auto target=dirty_, previous=attempts_;
    if(synced_>=target) return;
    urgent_=true; wake_.notify_all();
    wake_.wait(lock,[&]{return synced_>=target || (attempts_>previous && error_);});
    if(error_) throw std::system_error(error_,std::generic_category(),"Datafriend disk flush failed");
  }
  int stop() noexcept {
    { std::lock_guard lock(mutex_); stop_=true; wake_.notify_all(); }
    if(worker_.joinable()) worker_.join();
    return error_;
  }
#ifdef DATAFRIEND_TESTING
  uint64_t attempts() const { std::lock_guard lock(mutex_); return attempts_; }
#endif
};
} // namespace datafriend
