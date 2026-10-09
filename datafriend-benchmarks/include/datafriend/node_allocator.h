#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <utility>

namespace datafriend {
struct malloc_nodes {
  void* allocate(size_t bytes) { return std::malloc(bytes); }
  void deallocate(void* p, size_t) { std::free(p); }
};

// Single-threaded, shared across map roots. Size classes exactly match the
// wide tree's existing 24-byte header + 8-byte slots and capacity growth rule.
class slab_pool {
 public:
  using system_allocate_fn = void* (*)(size_t alignment, size_t bytes);
  static constexpr size_t slab_bytes = 65536, max_block_bytes = 4096;
  struct statistics {
    size_t reserved_bytes=0, peak_reserved_bytes=0, live_bytes=0;
    size_t allocations=0, slab_allocations=0, slab_frees=0;
    size_t fallback_allocations=0, fallback_live_bytes=0;
  };
 private:
  struct slab {
    slab *prev=nullptr, *next=nullptr, *all_prev=nullptr, *all_next=nullptr;
    void* free=nullptr;
    size_t block_bytes=0, issued=0, live=0, capacity=0;
  };
  struct bucket { slab *available=nullptr, *all=nullptr, *spare=nullptr; };
  static constexpr size_t data_offset = (sizeof(slab)+7)&~size_t(7);
  static constexpr auto class_table() {
    std::array<uint8_t,max_block_bytes/8+1> table{};
    size_t capacity=4; uint8_t id=1;
    while(24+8*capacity<=max_block_bytes) {
      table[(24+8*capacity)/8]=id++;
      capacity+=capacity/4 ? capacity/4 : 1;
    }
    return table;
  }
  std::array<bucket,64> buckets_{};
  statistics stats_;
  system_allocate_fn system_allocate_;
  static void* system_allocate(size_t alignment,size_t bytes) {
#if defined(__ANDROID__) && __ANDROID_API__ < 28
    if (!alignment) return std::malloc(bytes);
    void* p = nullptr;
    return posix_memalign(&p, alignment, bytes) == 0 ? p : nullptr;
#else
    return alignment ? std::aligned_alloc(alignment,bytes) : std::malloc(bytes);
#endif
  }
  static size_t class_id(size_t bytes) {
    static constexpr auto table=class_table();
    return bytes<=max_block_bytes && bytes%8==0 ? table[bytes/8] : 0;
  }
  static void add_available(bucket& b, slab* s) {
    s->prev=nullptr; s->next=b.available;
    if(s->next) s->next->prev=s;
    b.available=s;
  }
  static void remove_available(bucket& b, slab* s) {
    if(s->prev) s->prev->next=s->next; else b.available=s->next;
    if(s->next) s->next->prev=s->prev;
    s->prev=s->next=nullptr;
  }
  slab* make_slab(bucket& b,size_t bytes) {
    void* p=system_allocate_(slab_bytes,slab_bytes);
    if(!p) return nullptr;
    auto* s=new(p) slab;
    s->block_bytes=bytes; s->capacity=(slab_bytes-data_offset)/bytes;
    s->all_next=b.all;
    if(b.all) b.all->all_prev=s;
    b.all=s; add_available(b,s);
    stats_.reserved_bytes+=slab_bytes; ++stats_.slab_allocations;
    peak(); return s;
  }
  void drop_slab(bucket& b,slab* s) {
    assert(s->live==0);
    remove_available(b,s);
    if(s->all_prev) s->all_prev->all_next=s->all_next; else b.all=s->all_next;
    if(s->all_next) s->all_next->all_prev=s->all_prev;
    if(b.spare==s) b.spare=nullptr;
    s->~slab(); std::free(s);
    stats_.reserved_bytes-=slab_bytes; ++stats_.slab_frees;
  }
  void peak() {
    if(stats_.reserved_bytes>stats_.peak_reserved_bytes)
      stats_.peak_reserved_bytes=stats_.reserved_bytes;
  }
 public:
  explicit slab_pool(system_allocate_fn allocate=system_allocate) : system_allocate_(allocate) {}
  slab_pool(const slab_pool&)=delete;
  slab_pool& operator=(const slab_pool&)=delete;
  ~slab_pool() { assert(stats_.live_bytes==0); trim(); }
  const statistics& stats() const { return stats_; }
  void* allocate(size_t bytes) {
    const auto id=class_id(bytes);
    if(!id) {
      auto* p=system_allocate_(0,bytes); if(!p) return nullptr;
      ++stats_.allocations; stats_.live_bytes+=bytes;
      ++stats_.fallback_allocations; stats_.fallback_live_bytes+=bytes;
      stats_.reserved_bytes+=bytes; peak(); return p;
    }
    auto& b=buckets_[id];
    auto* s=b.available ? b.available : make_slab(b,bytes);
    if(!s) return nullptr;
    ++stats_.allocations; stats_.live_bytes+=bytes;
    assert(s->block_bytes==bytes);
    if(b.spare==s) b.spare=nullptr;
    void* p;
    if(s->free) { p=s->free; s->free=*static_cast<void**>(p); }
    else p=reinterpret_cast<char*>(s)+data_offset+(s->issued++)*bytes;
    if(++s->live==s->capacity) remove_available(b,s);
    return p;
  }
  void deallocate(void* p,size_t bytes) {
    assert(p && stats_.live_bytes>=bytes);
    stats_.live_bytes-=bytes;
    const auto id=class_id(bytes);
    if(!id) {
      std::free(p); stats_.reserved_bytes-=bytes; stats_.fallback_live_bytes-=bytes;
      return;
    }
    auto* s=reinterpret_cast<slab*>(reinterpret_cast<uintptr_t>(p)&~uintptr_t(slab_bytes-1));
    auto& b=buckets_[id];
    assert(s->block_bytes==bytes && s->live);
    if(s->live==s->capacity) add_available(b,s);
    // The node lifetime ended before release; freed storage holds the link.
    ::new(p) void*(s->free); s->free=p;
    if(--s->live==0) {
      if(b.spare) drop_slab(b,s);
      else b.spare=s;
    }
  }
  // Release the one retained empty slab per used class. Partial slabs remain.
  void trim() { for(auto& b:buckets_) if(b.spare) drop_slab(b,b.spare); }
};

struct slab_nodes {
  std::unique_ptr<slab_pool> owner;
  slab_pool* pool;
  slab_nodes() : owner(std::make_unique<slab_pool>()),pool(owner.get()) {}
  explicit slab_nodes(slab_pool& p) : pool(&p) {}
  slab_nodes(slab_nodes&&)=default;
  void* allocate(size_t bytes) { return pool->allocate(bytes); }
  void deallocate(void* p,size_t bytes) { pool->deallocate(p,bytes); }
};
} // namespace datafriend
