#ifndef APP_TEST_H
#define APP_TEST_H
#include "base.h"
#define CHECK(x)                                                                                   \
  do {                                                                                             \
    if (!(x)) {                                                                                    \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                      \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)
static inline void test_path(char *out, size_t size, const char *name) {
  snprintf(out, size, "/tmp/drawdo-%s-%ld", name, (long)getpid());
  CHECK(app_remove_tree(out));
}
#endif
