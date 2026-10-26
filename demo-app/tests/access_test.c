#include "accessible_ui/native.h"
#include "test.h"
static unsigned publications;
static unsigned notifications;
static void notified(void) { ++notifications; }
bool platform_accessibility_set(const access_snapshot *snapshot) {
  (void)snapshot;
  publications++;
  return true;
}
int main(void) {
  access_tree tree = {0};
  access_rect bounds = {10, 20, 100, 40};
  access_begin(&tree);
  CHECK(access_add(&tree, 1, bounds, "Save 🌱", "", ACCESS_BUTTON));
  CHECK(access_end(&tree) && publications == 1);
  for (int i = 0; i < 100; i++) {
    access_begin(&tree);
    CHECK(access_add(&tree, 1, bounds, "Save 🌱", "", ACCESS_BUTTON));
    CHECK(access_end(&tree) && publications == 1);
  }
  access_begin(&tree);
  CHECK(access_add(&tree, 1, bounds, "Save 🌱", "", ACCESS_BUTTON | ACCESS_DISABLED));
  CHECK(access_end(&tree) && publications == 2);
  CHECK(access_find(&tree, 1)->flags & ACCESS_DISABLED);
  access_begin(&tree);
  CHECK(access_add(&tree, 2, bounds, "New", "", ACCESS_BUTTON));
  CHECK(!access_add(&tree, 2, bounds, "Duplicate", "", ACCESS_BUTTON));
  CHECK(!access_end(&tree) && access_find(&tree, 1));
  access_begin(&tree);
  CHECK(access_end(&tree) && !access_find(&tree, 1));
  access_notify(notified);
  for (unsigned i = 1; i <= 16; i++)
    CHECK(platform_accessibility_send(i, ACCESS_ACTIVATE));
  CHECK(!platform_accessibility_send(17, ACCESS_ACTIVATE));
  CHECK(notifications == 16);
  access_notify(NULL);
  for (unsigned i = 1; i <= 16; i++) {
    access_event event;
    CHECK(access_poll(&event) && event.id == i && event.action == ACCESS_ACTIVATE);
  }
  access_event event;
  CHECK(!access_poll(&event));
  CHECK(platform_accessibility_send(1, ACCESS_ACTIVATE) && notifications == 16);
  CHECK(access_poll(&event));
  access_free(&tree);
  puts("PASS: accessibility snapshot caching, states, IDs, removal, queue ordering/backpressure");
  return 0;
}
