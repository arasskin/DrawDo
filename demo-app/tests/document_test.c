#include "text_crdt/crdt.h"
#include "test.h"
static void replace(document_sequence *s, const char *text) {
  bool changed;
  CHECK(sequence_replace(s, text, &changed));
}
static void merge(document_sequence *s, app_buffer *b) {
  bool changed;
  CHECK(sequence_merge(s, b->data, b->size, &changed));
  CHECK(sequence_merge(s, b->data, b->size, &changed) && !changed);
}
int main(void) {
  document_sequence seed, left, right;
  CHECK(sequence_open(&seed));
  replace(&seed, "Hello 🌍");
  CHECK(sequence_fork(&seed, &left) && sequence_fork(&seed, &right));
  replace(&left, "Hello brave 🌍");
  replace(&right, "Goodbye 🌍!");
  app_buffer states[3] = {{0}};
  CHECK(sequence_state(&seed, &states[0]) && sequence_state(&left, &states[1]) &&
        sequence_state(&right, &states[2]));
  unsigned orders[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  app_buffer expected = {0};
  for (int i = 0; i < 6; i++) {
    document_sequence s;
    CHECK(sequence_open(&s));
    for (int j = 0; j < 3; j++)
      merge(&s, &states[orders[i][j]]);
    if (!i)
      CHECK(app_string(&expected, app_cstr(&s.text), s.text.size));
    CHECK(!strcmp(app_cstr(&expected), app_cstr(&s.text)));
    sequence_close(&s);
  }
  document_sequence draft, changed;
  CHECK(sequence_fork(&seed, &draft) && sequence_fork(&seed, &changed));
  replace(&draft, "Hello 🌍?");
  replace(&changed, "Hello brave 🌍");
  app_buffer state = {0};
  CHECK(sequence_state(&draft, &state));
  merge(&changed, &state);
  CHECK(strstr(app_cstr(&changed.text), "brave"));
  replace(&draft, "Hello 🌍?!");
  CHECK(sequence_state(&draft, &state));
  merge(&changed, &state);
  CHECK(strstr(app_cstr(&changed.text), "brave"));
  const char *texts[] = {"🌍", "🌏", "é", "ê", "aéz", "aêz", "a😀z", "a😁z", "", "\n世界\n"};
  for (size_t i = 0; i < COUNT(texts); i++) {
    replace(&seed, texts[i]);
    CHECK(sequence_state(&seed, &state));
    document_sequence copy;
    CHECK(sequence_open(&copy));
    merge(&copy, &state);
    CHECK(!strcmp(app_cstr(&copy.text), texts[i]));
    sequence_close(&copy);
  }
  bool modified;
  CHECK(!sequence_replace(&seed, "\xc0\x80", &modified));
  char *large = malloc(20001);
  CHECK(large);
  memset(large, 'x', 20000);
  large[20000] = 0;
  replace(&seed, large);
  CHECK(sequence_state(&seed, &state));
  merge(&left, &state);
  replace(&seed, "");
  CHECK(sequence_state(&seed, &state));
  document_sequence empty;
  CHECK(sequence_open(&empty));
  merge(&empty, &state);
  CHECK(!empty.text.size);
  sequence_close(&empty);
  free(large);
  for (int i = 0; i < 3; i++)
    app_buffer_free(&states[i]);
  app_buffer_free(&state);
  app_buffer_free(&expected);
  sequence_close(&draft);
  sequence_close(&changed);
  sequence_close(&left);
  sequence_close(&right);
  sequence_close(&seed);
  puts("PASS: Yrs permutations, duplicate states, stale drafts, Unicode, deletion and 20K text");
  return 0;
}
