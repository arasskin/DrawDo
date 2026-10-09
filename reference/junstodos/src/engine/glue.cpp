#include "glue.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

#include "../sync/protocol.h"

namespace glue {

using edn::Value;

static const Value& kw_updated_at() {
    static const Value v = Value::keyword("updated-at");
    return v;
}

uint64_t now_ms() {
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count();
}

// ---- entity ids ---------------------------------------------------------------

static const char* kHex = "0123456789abcdef";

static std::string uuid_to_hex(const uint8_t* p) {
    std::string s(48, '0');
    for (int i = 0; i < 24; i++) {
        s[2 * i] = kHex[p[i] >> 4];
        s[2 * i + 1] = kHex[p[i] & 15];
    }
    return s;
}

static bool hex_to_uuid(const std::string& s, uint8_t out[24]) {
    if (s.size() != 48) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (int i = 0; i < 24; i++) {
        int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

Value new_entity_id() {
    uint8_t uuid[24];
    arc4random_buf(uuid, sizeof uuid);
    return Value::string(uuid_to_hex(uuid));
}

// ---- transact wrapper -----------------------------------------------------------

uint64_t entity_updated_at(const engine::Db& db, const Value& entity) {
    if (const auto* am = db.eav.find(entity)) {
        if (const Value* v = am->find(kw_updated_at())) {
            if (v->type() == edn::Type::Int) return (uint64_t)v->as_int();
        }
    }
    return 0;
}

engine::Db transact(engine::Db db, engine::Diff diff) {
    uint64_t now = now_ms();
    std::set<std::string> seen;  // by printed entity, to bump each once
    std::vector<Value> touched;
    for (const auto& [e, a, v] : diff.adds) {
        (void)a; (void)v;
        if (seen.insert(edn::pr_str(e)).second) touched.push_back(e);
    }
    for (const auto& [e, a] : diff.retracts) {
        (void)a;
        if (seen.insert(edn::pr_str(e)).second) touched.push_back(e);
    }
    for (const auto& e : touched) {
        uint64_t old = entity_updated_at(db, e);
        uint64_t ts = std::max(now, old + 1);  // never goes backwards
        diff.adds.push_back({e, kw_updated_at(), Value::integer((int64_t)ts)});
    }
    return engine::transact(std::move(db), diff);
}

engine::Db transact(engine::Db db, const Value& pattern) {
    if (pattern.type() != edn::Type::Map) {
        throw std::runtime_error("transact: pattern must be an EDN map {entity {attr val}}");
    }
    engine::Diff diff;
    for (const auto& [entity, attmap] : pattern.as_map()) {
        if (attmap.type() != edn::Type::Map) {
            throw std::runtime_error("transact: entity must map to an attr map");
        }
        for (const auto& [attr, value] : attmap.as_map()) {
            if (entity.is_symbol() || attr.is_symbol() || value.is_symbol()) {
                throw std::runtime_error("transact: pattern must be fully ground, got variable " +
                                         edn::pr_str(entity.is_symbol() ? entity
                                                     : attr.is_symbol() ? attr : value));
            }
            diff.adds.push_back({entity, attr, value});
        }
    }
    return glue::transact(std::move(db), std::move(diff));  // the bump overload
}

engine::Db transact(engine::Db db, const char* pattern_edn) {
    return glue::transact(std::move(db), edn::read_string(pattern_edn));
}

engine::Db retract_entity(engine::Db db, const Value& entity) {
    engine::Diff diff;
    if (const auto* am = db.eav.find(entity)) {
        for (const auto& [a, v] : *am) {
            (void)v;
            diff.retracts.push_back({entity, a});
        }
    }
    return engine::transact(std::move(db), diff);  // no bump: this is deletion
}

// ---- storage <-> memory -----------------------------------------------------------

engine::Db load(store::Store* s) {
    engine::Diff diff;
    store::load_all(s, [&](const store::Item& it) {
        uint8_t uuid[24];
        memcpy(uuid, it.id.data() + 8, 24);  // version id = [ts8 | uuid24]
        Value entity = Value::string(uuid_to_hex(uuid));
        Value attmap;
        try {
            attmap = edn::read_string(it.payload);
        } catch (const std::exception& e) {
            fprintf(stderr, "glue: skipping unparseable payload: %s\n", e.what());
            return;
        }
        if (attmap.type() != edn::Type::Map) {
            fprintf(stderr, "glue: skipping non-map payload\n");
            return;
        }
        bool has_ts = false;
        for (const auto& [a, v] : attmap.as_map()) {
            diff.adds.push_back({entity, a, v});
            if (a == kw_updated_at()) has_ts = true;
        }
        if (!has_ts) {  // repair from the key
            diff.adds.push_back({entity, kw_updated_at(),
                                 Value::integer((int64_t)it.updated_at)});
        }
    });
    return engine::transact(engine::Db{}, diff);  // raw transact: no bumps on load
}

struct MemRec {
    store::Id vid;  // [ts8 | uuid24] — sorts exactly like the store key
    uint64_t ts;
    std::string payload;
};

static std::vector<MemRec> collect_memory(const engine::Db& db) {
    std::vector<MemRec> recs;
    for (const auto& [e, am] : db.eav) {
        if (am.size() == 0) continue;  // fully retracted == deleted
        if (e.type() != edn::Type::String) {
            fprintf(stderr, "glue: skipping non-string entity id %s\n", edn::pr_str(e).c_str());
            continue;
        }
        uint8_t uuid[24];
        if (!hex_to_uuid(e.as_string(), uuid)) {
            fprintf(stderr, "glue: skipping non-uuid entity id %s\n", e.as_string().c_str());
            continue;
        }
        uint64_t ts = 0;
        if (const Value* v = am.find(kw_updated_at()); v && v->type() == edn::Type::Int) {
            ts = (uint64_t)v->as_int();
        } else {
            fprintf(stderr, "glue: skipping entity without :updated-at\n");
            continue;
        }
        edn::MapPairs pairs;
        for (const auto& [a, v] : am) pairs.emplace_back(a, v);
        MemRec r;
        r.vid = jsync::make_version_id(ts, uuid);
        r.ts = ts;
        r.payload = edn::pr_str(Value::map(std::move(pairs)));
        recs.push_back(std::move(r));
    }
    std::sort(recs.begin(), recs.end(), [](const MemRec& a, const MemRec& b) {
        return memcmp(a.vid.data(), b.vid.data(), store::ID_SIZE) < 0;
    });
    return recs;
}

bool sync_to_store(const engine::Db& db, store::Store* s) {
    std::vector<MemRec> mem = collect_memory(db);
    std::vector<store::Item> disk;
    store::load_all(s, [&](const store::Item& it) { disk.push_back(it); });
    // both sides are now in (updated-at, uuid) order: merge-walk
    size_t i = 0, j = 0;
    struct Op { bool put; uint64_t ts; store::Id id; std::string payload; };
    std::vector<Op> ops;
    while (i < mem.size() || j < disk.size()) {
        int cmp;
        if (i >= mem.size()) cmp = 1;
        else if (j >= disk.size()) cmp = -1;
        else cmp = memcmp(mem[i].vid.data(), disk[j].id.data(), store::ID_SIZE);
        if (cmp < 0) {  // memory only: put
            ops.push_back({true, mem[i].ts, mem[i].vid, mem[i].payload});
            i++;
        } else if (cmp > 0) {  // disk only: delete
            ops.push_back({false, disk[j].updated_at, disk[j].id, {}});
            j++;
        } else {  // same key: heal payload drift if any
            if (mem[i].payload != disk[j].payload) {
                ops.push_back({true, mem[i].ts, mem[i].vid, mem[i].payload});
            }
            i++, j++;
        }
    }
    if (ops.empty()) return false;
    store::write(s, [&](store::Writer& w) {
        for (const auto& op : ops) {
            if (op.put) w.put(op.ts, op.id, op.payload);
            else w.del(op.ts, op.id);
        }
    });
    return true;
}

}  // namespace glue
