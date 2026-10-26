#include "loop.h"
#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
static CFRunLoopRef owner;
static CFRunLoopSourceRef wake_source, socket_source;
static CFRunLoopTimerRef timer;
static CFFileDescriptorRef socket_ref;
static int socket_fd = -1;
static void (*process_events)(void);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void dispatch(void *unused) {
  (void)unused;
  if (process_events)
    process_events();
}
static void timed(CFRunLoopTimerRef t, void *unused) {
  (void)t;
  dispatch(unused);
}
static void readable(CFFileDescriptorRef f, CFOptionFlags flags, void *unused) {
  (void)f;
  (void)flags;
  dispatch(unused);
}
bool loop_open(void (*process)(void)) {
  pthread_mutex_lock(&lock);
  owner = CFRunLoopGetCurrent();
  CFRunLoopSourceContext context = {0};
  context.perform = dispatch;
  wake_source = CFRunLoopSourceCreate(NULL, 0, &context);
  pthread_mutex_unlock(&lock);
  timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1e9, 1e9, 0, 0, timed, NULL);
  if (!wake_source || !timer) {
    loop_close();
    return false;
  }
  CFRunLoopAddSource(owner, wake_source, kCFRunLoopCommonModes);
  CFRunLoopAddTimer(owner, timer, kCFRunLoopCommonModes);
  process_events = process;
  return true;
}
void loop_wake(void) {
  pthread_mutex_lock(&lock);
  if (wake_source) {
    CFRunLoopSourceSignal(wake_source);
    CFRunLoopWakeUp(owner);
  }
  pthread_mutex_unlock(&lock);
}
bool loop_watch(df_network_watch w) {
  if (socket_fd != w.fd) {
    if (socket_source) {
      CFRunLoopSourceInvalidate(socket_source);
      CFRelease(socket_source);
      socket_source = NULL;
    }
    if (socket_ref) {
      CFFileDescriptorInvalidate(socket_ref);
      CFRelease(socket_ref);
      socket_ref = NULL;
    }
    socket_fd = w.fd;
    if (socket_fd >= 0) {
      socket_ref = CFFileDescriptorCreate(NULL, socket_fd, false, readable, NULL);
      if (!socket_ref)
        return false;
      socket_source = CFFileDescriptorCreateRunLoopSource(NULL, socket_ref, 0);
      if (!socket_source)
        return false;
      CFRunLoopAddSource(owner, socket_source, kCFRunLoopCommonModes);
    }
  }
  if (socket_ref) {
    CFOptionFlags flags = kCFFileDescriptorReadCallBack;
    if (w.events & DF_NETWORK_WRITE)
      flags |= kCFFileDescriptorWriteCallBack;
    CFFileDescriptorDisableCallBacks(socket_ref, kCFFileDescriptorReadCallBack |
                                                     kCFFileDescriptorWriteCallBack);
    CFFileDescriptorEnableCallBacks(socket_ref, flags);
  }
  double delay = w.timeout_us == UINT64_MAX ? 1e9 : (double)w.timeout_us / 1e6;
  CFRunLoopTimerSetNextFireDate(timer, CFAbsoluteTimeGetCurrent() + delay);
  return true;
}
void loop_close(void) {
  process_events = NULL;
  pthread_mutex_lock(&lock);
  if (wake_source) {
    CFRunLoopSourceInvalidate(wake_source);
    CFRelease(wake_source);
    wake_source = NULL;
  }
  if (timer) {
    CFRunLoopTimerInvalidate(timer);
    CFRelease(timer);
    timer = NULL;
  }
  if (socket_source) {
    CFRunLoopSourceInvalidate(socket_source);
    CFRelease(socket_source);
    socket_source = NULL;
  }
  if (socket_ref) {
    CFFileDescriptorInvalidate(socket_ref);
    CFRelease(socket_ref);
    socket_ref = NULL;
  }
  socket_fd = -1;
  owner = NULL;
  pthread_mutex_unlock(&lock);
}
