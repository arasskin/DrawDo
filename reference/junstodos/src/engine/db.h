#pragma once
#include <optional>
#include <vector>

#include <immer/map.hpp>
#include <immer/set.hpp>

#include "value.h"

// C++ port of datafriend (icare.resources.datafriend), the in-memory half:
// an EAV triple store on immer persistent maps with the original's greedy
// backtracking query engine. `Db` is a value — transact returns a new one,
// snapshots are free, and the UI can hold one per frame.
//
// Deliberately NOT ported: transact-to-storage-async (replaced by the
// mirror/sync stack), open's row scan (replaced by store::load_all + payload
// parse in the glue layer).
//
// Faithful-port notes:
//  - Variables are edn symbols in entity or value position. Variable
//    ATTRIBUTES throw (unsupported in the original's execute; now explicit).
//  - Bindings are injective (sym->val AND val->sym), exactly like the
//    original. KNOWN QUIRK (Alex, 2026-07-10): this forbids two different
//    variables binding the same value in one solution; keep for now,
//    revisit later.
//  - Retracts apply before adds, and retraction uses a sentinel internally
//    so stored falsy values (false, nil) retract correctly.

namespace engine {

using Entity = edn::Value;
using Attr = edn::Value;

template <class K, class V>
using PMap = immer::map<K, V, edn::ValueHash, edn::ValueEq>;

using AttMap = PMap<Attr, edn::Value>;

struct Db {
    PMap<Entity, AttMap> eav;
    PMap<Attr, PMap<Entity, edn::Value>> aev;
    PMap<Attr, PMap<edn::Value, immer::set<Entity, edn::ValueHash, edn::ValueEq>>> ave;
};

// A transaction diff: retracts apply first, then adds. Mirrors the original's
// {'add {e {a v}}, 'retract {e {a _}}} shape, flattened.
struct Diff {
    std::vector<std::tuple<Entity, Attr, edn::Value>> adds;
    std::vector<std::pair<Entity, Attr>> retracts;
};

Db transact(Db db, const Diff& diff);

// Query: pattern is an EDN map {entity {attr val} ...} where entity and
// value positions may be symbols (variables). Returns the matched triples as
// a nested map {entity {attr value}} (empty map when nothing matched).
// Throws std::runtime_error if an attribute position holds a symbol.
//
// Patterns are values: build them with M/kw/sym, placing runtime data
// directly — query(db, M{{entity, M{{kw("done"), sym("?d")}}}}). The string
// overload is read_string convenience for fully constant patterns.
edn::Value query(const Db& db, const edn::Value& pattern);
edn::Value query(const Db& db, const char* pattern_edn);

}  // namespace engine
