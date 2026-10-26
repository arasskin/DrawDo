#include "accessible_ui/native.h"
#include <pthread.h>
/* Only discrete assistive actions cross threads, never model pointers. */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static access_event events[16];
static unsigned head, count;
static void (*notify)(void);
void access_notify(void (*wake)(void)) {
  pthread_mutex_lock(&lock);
  notify = wake;
  pthread_mutex_unlock(&lock);
}
bool platform_accessibility_send(uint64_t id, unsigned action) {
  if (!id || action < ACCESS_ACTIVATE || action > ACCESS_BACKWARD)
    return false;
  pthread_mutex_lock(&lock);
  bool ok = count < (sizeof(events) / sizeof(events[0]));
  if (ok)
    events[(head + count++) % (sizeof(events) / sizeof(events[0]))] = (access_event){id, action};
  if (ok && notify)
    notify();
  pthread_mutex_unlock(&lock);
  return ok;
}
bool access_poll(access_event *event) {
  pthread_mutex_lock(&lock);
  bool ok = count != 0;
  if (ok) {
    *event = events[head];
    head = (head + 1) % (sizeof(events) / sizeof(events[0]));
    count--;
  }
  pthread_mutex_unlock(&lock);
  return ok;
}
