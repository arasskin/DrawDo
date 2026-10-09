#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Independent range reconciliation for chosen 64-bit IDs and independent 64-bit fingerprints.
// Plain caller-owned state; no allocations, exceptions, or persistent metadata.
namespace reconciliation {
constexpr size_t ID_BYTES = 8;
constexpr size_t FINGERPRINT_BYTES = 8;
constexpr size_t LEAF_IDS = 32;
constexpr size_t FANOUT = 16;
constexpr uint8_t MAGIC[] = {'L', 'B', 'R', 3};
constexpr size_t MAX_SPLIT_BYTES = FANOUT * (2 + 16 + FINGERPRINT_BYTES);
constexpr size_t MAX_LIST_BYTES = 2 + 16 + 1 + LEAF_IDS * (ID_BYTES + FINGERPRINT_BYTES);
constexpr size_t MIN_OUTPUT_CAPACITY = sizeof(MAGIC) +
  (MAX_SPLIT_BYTES > MAX_LIST_BYTES ? MAX_SPLIT_BYTES : MAX_LIST_BYTES);

// [low, high). high=0 means infinity, so {} covers every ID, including UINT64_MAX.
struct range { uint64_t low = 0, high = 0; };
inline bool valid(range r) { return r.high == 0 || r.low < r.high; }
inline bool contains(range r, uint64_t id) { return id >= r.low && (r.high == 0 || id < r.high); }
inline bool upper_bound(range r, uint64_t *out) {
  if (r.high == 0) return false;
  *out = r.high;
  return true;
}
struct summary { uint64_t count = 0, fingerprint = 0; };
struct item { uint64_t id = 0, fingerprint = 0; };
// All callbacks read one stable snapshot. summarize uses cached count/XOR
// accumulators; select returns the ID at a global zero-based rank. list writes
// the first min(size,capacity) items sorted by unique ID, or SIZE_MAX on error.
// Range XORs combine stored fingerprints, never IDs. The caller chooses both.
struct source {
  void *context = nullptr;
  bool (*summarize)(void *, range, summary *) = nullptr;
  bool (*select)(void *, uint64_t, uint64_t *) = nullptr;
  size_t (*list)(void *, range, item *, size_t) = nullptr;
};
struct bytes { const uint8_t *data = nullptr; size_t size = 0; };
struct output {
  uint8_t *data = nullptr;
  size_t capacity = 0, size = 0;
  uint64_t previous_high = 0;
};
// A shared ID with unequal fingerprints appears in BOTH have and need. The
// driver applies namespace authority to choose the version; this is not deletion.
struct differences {
  uint64_t *have = nullptr;
  size_t have_capacity = 0, have_count = 0;
  uint64_t *need = nullptr;
  size_t need_capacity = 0, need_count = 0;
};
// One cursor per incoming packet. Keep packet bytes alive while resuming it.
struct cursor {
  size_t offset = 0;
  range working{};
  bool traversing = false;
  uint64_t previous_high = 0;
};
enum class role { initiator, responder };
enum class status { complete, more, output_full, differences_full, invalid_input, source_error, invalid_argument };
enum class kind : uint8_t { summary = 0, offer = 1, answer = 2 };
struct workspace {
  item own[LEAF_IDS + 1]; // Also holds the 16 cut boundaries while refining.
  item remote[LEAF_IDS];
  uint64_t have[LEAF_IDS], need[LEAF_IDS];
  uint64_t summaries[FANOUT];
};
struct frame { range part{}; kind type = kind::summary; uint64_t fingerprint = 0; size_t count = 0; };

inline size_t integer_bytes(uint64_t value) {
  size_t width = 0;
  while (value) { ++width; value >>= 8; }
  return width;
}
// Omit whole ID bytes shared by every integer in this range.
inline size_t suffix_bytes(range r) { return integer_bytes(r.low ^ (r.high ? r.high - 1 : UINT64_MAX)); }
inline size_t frame_size(range r, kind type, size_t count = 0) {
  return 2 + integer_bytes(r.low) + integer_bytes(r.high) +
    (type == kind::summary ? FINGERPRINT_BYTES : 1 + count * (suffix_bytes(r) + FINGERPRINT_BYTES));
}
inline void write_integer(output *out, uint64_t value, size_t width) {
  for (size_t i = width; i != 0; --i) out->data[out->size++] = uint8_t(value >> (8 * (i - 1)));
}
inline void write_frame(output *out, range r, kind type, const item *items, size_t count, uint64_t fingerprint = 0) {
  // A first frame is always self-contained, so streamed chunks can be joined.
  const bool adjacent = out->previous_high != 0 && r.low == out->previous_high;
  out->data[out->size++] = uint8_t(type);
  out->data[out->size++] = uint8_t(((adjacent ? 9 : integer_bytes(r.low)) << 4) | integer_bytes(r.high));
  if (!adjacent) write_integer(out, r.low, integer_bytes(r.low));
  write_integer(out, r.high, integer_bytes(r.high));
  out->previous_high = r.high;
  if (type == kind::summary) write_integer(out, fingerprint, FINGERPRINT_BYTES);
  else {
    out->data[out->size++] = uint8_t(count);
    for (size_t i = 0; i < count; ++i) {
      write_integer(out, items[i].id, suffix_bytes(r));
      write_integer(out, items[i].fingerprint, FINGERPRINT_BYTES);
    }
  }
}
inline bool take(bytes packet, size_t *position, size_t width, uint64_t *value) {
  if (*position > packet.size || width > packet.size - *position) return false;
  *value = 0;
  for (size_t i = 0; i < width; ++i) *value = (*value << 8) | packet.data[(*position)++];
  return true;
}
inline bool read_frame(bytes packet, size_t *position, frame *f, workspace *w, uint64_t previous_high = 0) {
  uint64_t type, widths;
  if (!take(packet, position, 1, &type) || type > uint8_t(kind::answer) ||
      !take(packet, position, 1, &widths)) return false;
  f->type = kind(type);
  const size_t low_width = widths >> 4, high_width = widths & 15;
  if (low_width > 9 || high_width > 8 || (low_width == 9 && previous_high == 0)) return false;
  f->part.low = previous_high;
  if ((low_width != 9 && (!take(packet, position, low_width, &f->part.low) || integer_bytes(f->part.low) != low_width)) ||
      !take(packet, position, high_width, &f->part.high) ||
      integer_bytes(f->part.high) != high_width || !valid(f->part)) return false;
  if (f->type == kind::summary) return take(packet, position, FINGERPRINT_BYTES, &f->fingerprint);
  uint64_t value;
  if (!take(packet, position, 1, &value) || value > LEAF_IDS) return false;
  f->count = size_t(value);
  const size_t suffix = suffix_bytes(f->part);
  const uint64_t shared = suffix == 8 ? 0 : f->part.low & (UINT64_MAX << (suffix * 8));
  for (size_t i = 0; i < f->count; ++i) {
    if (!take(packet, position, suffix, &value)) return false;
    w->remote[i].id = shared | value;
    if (!take(packet, position, FINGERPRINT_BYTES, &w->remote[i].fingerprint)) return false;
    if (!contains(f->part, w->remote[i].id) || (i != 0 && w->remote[i - 1].id >= w->remote[i].id)) return false;
  }
  return true;
}
inline bool read_own(source src, range r, workspace *w, size_t *count) {
  *count = src.list(src.context, r, w->own, LEAF_IDS + 1);
  if (*count > LEAF_IDS + 1) return false;
  for (size_t i = 0; i < *count; ++i)
    if (!contains(r, w->own[i].id) || (i != 0 && w->own[i - 1].id >= w->own[i].id)) return false;
  return true;
}
inline bool initialize_output(output *out) {
  out->size = 0;
  out->previous_high = 0;
  if (out->data == nullptr || out->capacity < sizeof(MAGIC)) return false;
  memcpy(out->data, MAGIC, sizeof(MAGIC));
  out->size = sizeof(MAGIC);
  return true;
}
inline bool room(const output *out, size_t needed) { return needed <= out->capacity - out->size; }

// Partition by accumulated record count. Each group has n/16 records, with one
// extra in the first n%16 groups. Rank selection supplies actual ID boundaries.
// Keep the parent's full extent, including gaps containing only the peer's IDs.
inline status refine(source src, range parent, uint64_t count, workspace *w, output *out) {
  if (count <= LEAF_IDS) return status::source_error;
  summary before{};
  if (parent.low && !src.summarize(src.context, {0, parent.low}, &before)) return status::source_error;
  if (count > UINT64_MAX - before.count) return status::source_error;
  uint64_t rank = before.count, low = parent.low;
  const uint64_t per_group = count / FANOUT, extra = count % FANOUT;
  size_t needed = 0;
  for (size_t i = 0; i < FANOUT; ++i) {
    rank += per_group + (i < extra);
    uint64_t high = parent.high;
    if (i + 1 != FANOUT && (!src.select(src.context, rank, &high) || high <= low || !contains(parent, high)))
      return status::source_error;
    w->own[i].id = high;
    needed += frame_size({low, high}, kind::summary);
    low = high;
  }
  if (!room(out, needed)) return status::output_full;
  low = parent.low;
  for (size_t i = 0; i < FANOUT; ++i) {
    summary child;
    if (!src.summarize(src.context, {low, w->own[i].id}, &child) || child.count != per_group + (i < extra))
      return status::source_error;
    w->summaries[i] = child.fingerprint;
    low = w->own[i].id;
  }
  low = parent.low;
  for (size_t i = 0; i < FANOUT; ++i) {
    write_frame(out, {low, w->own[i].id}, kind::summary, nullptr, 0, w->summaries[i]);
    low = w->own[i].id;
  }
  return status::complete;
}
// Preserve the small opening: one whole-namespace XOR for larger sets. The
// count accumulator avoids probing IDs until explicit enumeration is needed.
inline status begin(source src, workspace *w, output *out) {
  if (out) out->size = 0;
  if (!src.list || !src.summarize || !src.select || !w || !out) return status::invalid_argument;
  if (!initialize_output(out)) return status::output_full;
  summary root;
  if (!src.summarize(src.context, {}, &root)) return status::source_error;
  if (root.count <= LEAF_IDS) {
    if (!room(out, frame_size({}, kind::offer, size_t(root.count)))) return status::output_full;
    size_t count;
    if (!read_own(src, {}, w, &count) || count != root.count) return status::source_error;
    write_frame(out, {}, kind::offer, w->own, count);
  } else {
    if (!room(out, frame_size({}, kind::summary))) return status::output_full;
    write_frame(out, {}, kind::summary, nullptr, 0, root.fingerprint);
  }
  return status::complete;
}

// Output/difference counts reset each call. *_full and more are resumable:
// drain results and retain the packet/cursor. Only the initiator gets have/need.
// A visit handles one summary (up to 16 child summaries) or at most 32 local IDs.
// Input must not overlap output, differences, or workspace. Finish a direction
// only once every input chunk is consumed and no response frames remain.
inline status receive(source src, role side, bytes packet, cursor *at, workspace *w,
                      output *out, differences *diff, size_t work_budget = 64) {
  if (out) out->size = 0;
  if (diff) diff->have_count = diff->need_count = 0;
  if (!src.list || !src.summarize || !src.select || !w || !out || !diff || !at || !packet.data || work_budget == 0 ||
      (diff->have_capacity && !diff->have) || (diff->need_capacity && !diff->need)) return status::invalid_argument;
  if (packet.size < sizeof(MAGIC) || memcmp(packet.data, MAGIC, sizeof(MAGIC)) != 0 ||
      (at->offset != 0 && at->offset < sizeof(MAGIC)) || at->offset > packet.size) return status::invalid_input;
  if (!initialize_output(out)) return status::output_full;
  if (at->offset == 0) at->offset = sizeof(MAGIC);
  size_t visits = 0;
  while (at->offset != packet.size && visits < work_budget) {
    size_t end = at->offset;
    frame incoming{};
    if (!read_frame(packet, &end, &incoming, w, at->previous_high)) return status::invalid_input;
    if (incoming.type == kind::summary) {
      if (at->traversing) return status::invalid_input;
      ++visits;
      summary own;
      if (!src.summarize(src.context, incoming.part, &own)) return status::source_error;
      if (incoming.fingerprint != own.fingerprint) {
        if (own.count > LEAF_IDS) {
          const status refined = refine(src, incoming.part, own.count, w, out);
          if (refined != status::complete) return refined;
        } else {
          if (!room(out, frame_size(incoming.part, kind::offer, size_t(own.count)))) return status::output_full;
          size_t count;
          if (!read_own(src, incoming.part, w, &count) || count != own.count) return status::source_error;
          write_frame(out, incoming.part, kind::offer, w->own, count);
        }
      }
      at->offset = end;
      at->previous_high = incoming.part.high;
      continue;
    }
    if (side == role::responder && incoming.type == kind::answer) return status::invalid_input;
    if (!at->traversing) { at->working = incoming.part; at->traversing = true; }
    if (!valid(at->working) || at->working.high != incoming.part.high || !contains(incoming.part, at->working.low))
      return status::invalid_input;
    while (visits < work_budget) {
      ++visits;
      size_t own_count;
      if (!read_own(src, at->working, w, &own_count)) return status::source_error;
      range chunk = at->working;
      const bool remaining = own_count > LEAF_IDS;
      if (remaining) { chunk.high = w->own[LEAF_IDS].id; own_count = LEAF_IDS; }
      if (side == role::responder) {
        if (!room(out, frame_size(chunk, kind::answer, own_count))) return status::output_full;
        write_frame(out, chunk, kind::answer, w->own, own_count);
      } else {
        size_t first = 0;
        while (first < incoming.count && w->remote[first].id < chunk.low) ++first;
        size_t count = 0;
        while (first + count < incoming.count && contains(chunk, w->remote[first + count].id)) ++count;
        const item *remote = w->remote + first;
        size_t i = 0, j = 0, have = 0, need = 0;
        while (i < own_count || j < count) {
          if (j == count || (i < own_count && w->own[i].id < remote[j].id)) w->have[have++] = w->own[i++].id;
          else if (i == own_count || remote[j].id < w->own[i].id) w->need[need++] = remote[j++].id;
          else {
            if (w->own[i].fingerprint != remote[j].fingerprint) {
              w->have[have++] = w->own[i].id;
              w->need[need++] = remote[j].id;
            }
            ++i; ++j;
          }
        }
        if (have > diff->have_capacity - diff->have_count || need > diff->need_capacity - diff->need_count)
          return status::differences_full;
        if (have) memcpy(diff->have + diff->have_count, w->have, have * sizeof(uint64_t));
        if (need) memcpy(diff->need + diff->need_count, w->need, need * sizeof(uint64_t));
        diff->have_count += have; diff->need_count += need;
      }
      if (!remaining) {
        at->traversing = false;
        at->offset = end;
        at->previous_high = incoming.part.high;
        break;
      }
      at->working.low = chunk.high;
    }
  }
  return at->offset == packet.size ? status::complete : status::more;
}
static_assert(sizeof(item) == 16);
static_assert(sizeof(workspace) == 1680);
} // namespace reconciliation
