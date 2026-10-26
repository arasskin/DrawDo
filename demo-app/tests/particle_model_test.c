#include "particle_model.h"
#include "test.h"
int main(void) {
  char path[128];
  test_path(path, sizeof(path), "particles-c99");
  particle_app a;
  if (!particles_open(&a, path)) {
    fprintf(stderr, "open: %s\n", a.db.error);
    return 1;
  }
  CHECK(a.count == 1000 && a.local && !a.paused && a.automatic && particles_validate(&a));
  uint64_t button_builds = particles_button_builds(&a);
  uint64_t revision = df_revision(a.db.engine);
  for (int i = 0; i < 100; i++)
    CHECK(particles_step(&a));
  CHECK(df_revision(a.db.engine) > revision && particles_button_builds(&a) == button_builds);
  CHECK(particles_validate(&a));
  CHECK(particles_press(&a, 6));
  revision = df_revision(a.db.engine);
  int tick = a.tick;
  for (int i = 0; i < 3; i++)
    CHECK(particles_step(&a));
  CHECK(df_revision(a.db.engine) == revision && a.tick == tick);
  void *views[96];
  for (int i = 0; i < 96; i++)
    views[i] = a.cells[i].watch->result;
  CHECK(particles_patch(&a, 99, -8));
  CHECK(a.column == 7 && a.row == 0 && !a.automatic);
  const int counts[] = {100, 300, 1000, 3000};
  for (int i = 0; i < 4; i++) {
    CHECK(particles_count(&a, counts[i]));
    CHECK(a.count == counts[i] && a.paused && !a.automatic && particles_validate(&a));
    for (int j = 0; j < 96; j++)
      CHECK(views[j] == a.cells[j].watch->result);
    CHECK(a.buttons[i].selected);
  }
  CHECK(!particles_count(&a, 17));
  a.db.error[0] = 0;
  CHECK(particles_press(&a, 6) && particles_press(&a, 5));
  CHECK(!a.paused && !a.local);
  for (int i = 0; i < 3; i++)
    CHECK(particles_step(&a));
  CHECK(particles_validate(&a));
  particles_close(&a);
  CHECK(app_remove_tree(path));
  puts("PASS: particle controls, stable views, pause, atomic count replacement, publication and "
       "physics");
  return 0;
}
