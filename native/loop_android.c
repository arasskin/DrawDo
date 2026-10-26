#define _POSIX_C_SOURCE 200809L
#include "loop.h"
#include <android/looper.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <errno.h>
static ALooper *owner;
static int wake_fd = -1, timer_fd = -1, socket_fd = -1;
static unsigned socket_events;
static void (*process_events)(void);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int dispatch(int fd, int events, void *context) {
  (void)events;
  (void)context;
  if (fd == wake_fd || fd == timer_fd) {
    uint64_t value;
    while (read(fd, &value, sizeof(value)) < 0 && errno == EINTR) {
    }
  }
  if (process_events)
    process_events();
  return 1;
}
bool loop_open(void (*process)(void)) {
  owner = ALooper_forThread();
  if (!owner)
    return false;
  pthread_mutex_lock(&lock);
  wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  pthread_mutex_unlock(&lock);
  timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (wake_fd < 0 || timer_fd < 0 ||
      ALooper_addFd(owner, wake_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, dispatch, NULL) <
          0 ||
      ALooper_addFd(owner, timer_fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, dispatch, NULL) <
          0) {
    loop_close();
    return false;
  }
  process_events = process;
  return true;
}
void loop_wake(void) {
  pthread_mutex_lock(&lock);
  uint64_t one = 1;
  if (wake_fd >= 0) {
    while (write(wake_fd, &one, sizeof(one)) < 0 && errno == EINTR) {
    }
  }
  pthread_mutex_unlock(&lock);
}
bool loop_watch(df_network_watch w) {
  if (socket_fd != w.fd || socket_events != w.events) {
    if (socket_fd >= 0)
      ALooper_removeFd(owner, socket_fd);
    socket_fd = w.fd;
    socket_events = w.events;
    if (socket_fd >= 0) {
      int flags = ALOOPER_EVENT_INPUT;
      if (w.events & DF_NETWORK_WRITE)
        flags |= ALOOPER_EVENT_OUTPUT;
      if (ALooper_addFd(owner, socket_fd, ALOOPER_POLL_CALLBACK, flags, dispatch, NULL) < 0)
        return false;
    }
  }
  struct itimerspec timer = {0};
  if (w.timeout_us != UINT64_MAX) {
    timer.it_value.tv_sec = (time_t)(w.timeout_us / 1000000);
    timer.it_value.tv_nsec = w.timeout_us ? (long)(w.timeout_us % 1000000) * 1000 : 1;
  }
  return timerfd_settime(timer_fd, 0, &timer, NULL) == 0;
}
void loop_close(void) {
  process_events = NULL;
  pthread_mutex_lock(&lock);
  if (owner) {
    if (socket_fd >= 0)
      ALooper_removeFd(owner, socket_fd);
    if (wake_fd >= 0)
      ALooper_removeFd(owner, wake_fd);
    if (timer_fd >= 0)
      ALooper_removeFd(owner, timer_fd);
  }
  if (wake_fd >= 0)
    close(wake_fd);
  if (timer_fd >= 0)
    close(timer_fd);
  wake_fd = timer_fd = socket_fd = -1;
  socket_events = 0;
  owner = NULL;
  pthread_mutex_unlock(&lock);
}
