#ifndef APP_PARTICLE_MODEL_H
#define APP_PARTICLE_MODEL_H
#include "datafriend_ui.h"
enum { PARTICLE_COLS = 8, PARTICLE_ROWS = 12, PARTICLE_CELLS = 96, PARTICLE_CONTROL = 10000 };
typedef struct particle {
  uint32_t id;
  double x, y;
} particle;
typedef struct particle_update {
  particle value;
  bool crossing;
} particle_update;
typedef struct particle_cell {
  df_watch *watch;
  particle *items;
  size_t count, capacity;
  bool active;
  void *owner;
} particle_cell;
typedef struct particle_sample {
  int64_t physics, prepare, edit, readback, total;
  int moved, crossings, changed, reads;
  uint64_t pairs, interactions;
} particle_sample;
typedef struct particle_button {
  char label[24];
  bool selected;
  uint64_t builds;
  int index;
  void *owner;
  df_watch *watch;
} particle_button;
typedef struct particle_app {
  ui_store db;
  particle_cell cells[PARTICLE_CELLS];
  double *drift;
  size_t drift_capacity;
  int count, tick, column, row;
  bool paused, local, automatic;
  const char *status;
  uint64_t builds;
  particle_button buttons[7];
  df_watch *settings;
  particle_sample last;
  particle_update *updates;
  particle *neighbors;
  size_t update_capacity, neighbor_capacity;
  int refreshed, reads;
} particle_app;
bool particles_open(particle_app *, const char *);
void particles_close(particle_app *);
bool particles_count(particle_app *, int);
bool particles_press(particle_app *, int);
bool particles_patch(particle_app *, int, int);
bool particles_step(particle_app *);
bool particles_validate(particle_app *);
uint64_t particles_button_builds(particle_app *);
int particles_cell(double, double);
#endif
