#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <charconv>

namespace parsing {

struct string_slice {
  char *start;
  int length;
};

inline string_slice chop_at(const string_slice buffer, int (*preposition)(int)) {
  char *iterator = buffer.start;
  while ((iterator < buffer.start + buffer.length) && !(preposition((unsigned char)*iterator))) iterator++;
  return string_slice{.start = buffer.start, .length = (int)(iterator - buffer.start)};
}

inline string_slice chop_next(const string_slice token, const string_slice buffer, int (*preposition)(int)) {
  char *end = buffer.start + buffer.length;
  char *iterator = token.start + token.length;

  while ((iterator < end) && (preposition((unsigned char)*iterator))) iterator++;
  char *start = iterator;

  while ((iterator < end) && !(preposition((unsigned char)*iterator))) iterator++;
  return string_slice{.start = start, .length = (int)(iterator - start)};
}

inline bool string_slice_equals(const string_slice ss, const char *s, int length) {
  return (ss.length == length && memcmp(ss.start, s, length) == 0);
}

struct maybe_value {uint64_t value; bool has_error;};

inline maybe_value ss_to_ull(const string_slice ss) {
  uint64_t number;
  if (auto [ptr, ec] = std::from_chars(ss.start, ss.start + ss.length, number);
      ec == std::errc() && ptr == ss.start + ss.length) {
    return maybe_value{.value = number, .has_error = false};
  }

  return maybe_value{.value = 0, .has_error = true};
}

inline void print_token(const char *s, string_slice token, const char *next_s = "") {
  printf("%s%.*s%s\n", s, token.length, token.start, next_s);
}

}
