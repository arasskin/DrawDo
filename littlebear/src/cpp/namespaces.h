#pragma once
namespace hash_table {
using namespace_identifier = lb_namespace;
using key = lb_namespace;
using value = record_index::header;
using allocate_fn = ct_allocate_fn;
constexpr key ALL_ONES{UINT64_MAX, UINT64_MAX};
constexpr size_t DEFAULT_INITIAL_CAPACITY = 0;
inline bool operator==(key a, key b) {
  return lb_namespace_equal(a, b);
}
inline void *system_allocate(size_t a, size_t n) {
  return ct_system_allocate(a, n);
}
struct directory : lb_directory {
  directory() { static_cast<lb_directory &>(*this) = {}; }
  directory(const directory &) = delete;
  directory(directory &&o) {
    static_cast<lb_directory &>(*this) = o;
    static_cast<lb_directory &>(o) = {};
  }
  directory &operator=(directory &&o) {
    lb_directory_destroy(this);
    static_cast<lb_directory &>(*this) = o;
    static_cast<lb_directory &>(o) = {};
    return *this;
  }
  ~directory() { lb_directory_destroy(this); }
};
inline lb_index convert(value v) {
  return {static_cast<ct_node *>(v.root), v.node_allocator, v.rank};
}
struct equal {
  bool operator()(key a, key b) const { return lb_namespace_equal(a, b); }
};
inline bool valid_capacity(size_t n) {
  return lb_directory_valid(n);
}
inline directory create(size_t n = 0, allocate_fn fn = system_allocate) {
  directory t;
  static_cast<lb_directory &>(t) = lb_directory_create(n, fn);
  return t;
}
inline bool bad(const lb_directory *t) {
  return !t->initialized;
}
inline void destroy(lb_directory *t) {
  lb_directory_destroy(t);
}
inline void clear(lb_directory *t) {
  lb_directory_clear(t);
}
inline size_t count(const lb_directory *t) {
  return t->count;
}
inline size_t memory_usage(const lb_directory *t) {
  return sizeof(directory) + t->buckets * sizeof(uint32_t) +
         t->capacity * sizeof(lb_namespace_entry);
}
inline value *get(lb_directory *t, key k) {
  return lb_directory_get(t, k);
}
inline bool contains(lb_directory *t, key k) {
  return get(t, k) != nullptr;
}
inline value unsafe_get(lb_directory *t, key k) {
  auto *v = get(t, k);
  assert(v);
  return *v;
}
inline bool reserve(lb_directory *t, size_t n) {
  return lb_directory_reserve(t, n);
}
inline bool unsafe_insert(lb_directory *t, key k, value v) {
  return lb_directory_insert(t, k, convert(v));
}
inline bool associate(lb_directory *t, key k, value v) {
  if (auto *p = get(t, k)) {
    *p = v;
    return true;
  }
  return unsafe_insert(t, k, v);
}
inline bool dissociate(lb_directory *t, key k) {
  return lb_directory_erase(t, k);
}
inline void unsafe_dissociate(lb_directory *t, key k) {
  bool erased = dissociate(t, k);
  assert(erased);
  (void)erased;
}
template <class F> void for_each(lb_directory *t, F &&f) {
  for (size_t i = 0; i < t->count; ++i) {
    auto &e = t->entries[i];
    f(e.key, e.value);
  }
}
} // namespace hash_table
