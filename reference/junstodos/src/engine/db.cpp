#include "db.h"

#include <algorithm>
#include <stdexcept>

namespace engine {

using edn::Value;

// stands in for the original's proprietary-nothing sentinel: a symbol no
// real data will ever contain (symbols aren't storable values in practice,
// and this exact one is namespaced to make collision a non-concern)
static const Value& nothing() {
    static const Value v = Value::symbol("datafriend/THIS-VALUE-REPRESENTS-NOTHING");
    return v;
}

static Value eav_get(const Db& db, const Entity& e, const Attr& a) {
    if (const AttMap* am = db.eav.find(e)) {
        if (const Value* v = am->find(a)) return *v;
    }
    return nothing();
}

// ---- transact ------------------------------------------------------------------

Db transact(Db db, const Diff& diff) {
    // retracts first
    for (const auto& [e, a] : diff.retracts) {
        Value old = eav_get(db, e, a);
        if (old == nothing()) continue;
        db.eav = db.eav.set(e, db.eav[e].erase(a));
        db.aev = db.aev.set(a, db.aev[a].erase(e));
        auto vmap = db.ave[a];
        db.ave = db.ave.set(a, vmap.set(old, vmap[old].erase(e)));
    }
    // then adds
    for (const auto& [e, a, v] : diff.adds) {
        Value old = eav_get(db, e, a);
        if (old != nothing()) {
            // remove only this entity from the old value's ave entry
            auto vmap = db.ave[a];
            db.ave = db.ave.set(a, vmap.set(old, vmap[old].erase(e)));
        }
        db.eav = db.eav.set(e, db.eav[e].set(a, v));
        db.aev = db.aev.set(a, db.aev[a].set(e, v));
        auto vmap = db.ave[a];
        db.ave = db.ave.set(a, vmap.set(v, vmap[v].insert(e)));
    }
    return db;
}

// ---- query engine -----------------------------------------------------------------

namespace {

struct Triple {
    Value e, a, v;
    bool operator==(const Triple& o) const { return e == o.e && a == o.a && v == o.v; }
};

// one-to-one bindings, exactly like the original's try-bind
struct Bindings {
    PMap<Value, Value> sym_to_val;
    PMap<Value, Value> val_to_sym;
};

std::optional<Bindings> try_bind(Bindings b, const Value& sym, const Value& val) {
    if (const Value* bound = b.sym_to_val.find(sym)) {
        if (*bound != val) return std::nullopt;
    }
    if (const Value* bound = b.val_to_sym.find(val)) {
        if (*bound != sym) return std::nullopt;
    }
    b.sym_to_val = b.sym_to_val.set(sym, val);
    b.val_to_sym = b.val_to_sym.set(val, sym);
    return b;
}

int triple_tier(const Triple& t) {
    int tier = 0;
    if (t.e.is_symbol()) tier += 2;
    if (t.v.is_symbol()) tier += 1;
    return tier;
}

using Pending = std::vector<std::pair<std::pair<Value, Value>, Value>>;  // [(e,a)] -> v
using Result = PMap<Entity, AttMap>;

Result commit_pending(Result result, const Pending& pending) {
    for (const auto& [ea, v] : pending) {
        const auto& [e, a] = ea;
        AttMap am;
        if (const AttMap* found = result.find(e)) am = *found;
        result = result.set(e, am.set(a, v));
    }
    return result;
}

std::optional<Result> execute(const Db& db, std::vector<Triple> triples, Result result,
                              Pending pending, Bindings bindings) {
    if (triples.empty()) return commit_pending(std::move(result), pending);

    // most-bound-first, stable on ties (matches clojure min-key's keep-first)
    auto best = std::min_element(triples.begin(), triples.end(),
                                 [](const Triple& x, const Triple& y) {
                                     return triple_tier(x) < triple_tier(y);
                                 });
    Triple t = *best;
    std::vector<Triple> remaining;
    for (const auto& other : triples) {
        if (!(other == t)) remaining.push_back(other);
    }

    // substitute already-bound variables
    Value e = t.e, v = t.v;
    if (e.is_symbol()) {
        if (const Value* bound = bindings.sym_to_val.find(e)) e = *bound;
    }
    if (v.is_symbol()) {
        if (const Value* bound = bindings.sym_to_val.find(v)) v = *bound;
    }

    const bool e_var = e.is_symbol(), v_var = v.is_symbol();

    if (!e_var && !v_var) {  // fully specified: verify against the db
        // (the original recorded ground-after-substitution triples unchecked;
        // fixed 2026-07-11 — a pattern constant must hold in the db to match)
        if (eav_get(db, e, t.a) != v) return std::nullopt;
        pending.push_back({{e, t.a}, v});
        return execute(db, std::move(remaining), std::move(result), std::move(pending), bindings);
    }

    if (!e_var && v_var) {  // lookup value by (e, a)
        Value found = eav_get(db, e, t.a);
        if (found == nothing()) return std::nullopt;
        auto b = try_bind(bindings, v, found);
        if (!b) return std::nullopt;
        pending.push_back({{e, t.a}, found});
        return execute(db, std::move(remaining), std::move(result), std::move(pending), *b);
    }

    if (e_var && !v_var) {  // entities by (a, v)
        const auto* vmap = db.ave.find(t.a);
        const auto* entities = vmap ? vmap->find(v) : nullptr;
        if (!entities || entities->empty()) return std::nullopt;
        for (const Entity& found : *entities) {
            if (auto b = try_bind(bindings, e, found)) {
                Pending p = pending;
                p.push_back({{found, t.a}, v});
                if (auto r = execute(db, remaining, result, std::move(p), *b)) {
                    result = std::move(*r);
                }
            }
        }
        return result;  // success even when no branch bound, like the original
    }

    // both variables: all (e, v) pairs of the attribute
    const auto* evpairs = db.aev.find(t.a);
    if (!evpairs || evpairs->empty()) return std::nullopt;
    for (const auto& [found_e, found_v] : *evpairs) {
        auto b1 = try_bind(bindings, e, found_e);
        if (!b1) continue;
        auto b2 = try_bind(*b1, v, found_v);
        if (!b2) continue;
        Pending p = pending;
        p.push_back({{found_e, t.a}, found_v});
        if (auto r = execute(db, remaining, result, std::move(p), *b2)) {
            result = std::move(*r);
        }
    }
    return result;
}

std::vector<Triple> parse_triples(const Value& pattern) {
    if (pattern.type() != edn::Type::Map) {
        throw std::runtime_error("query: pattern must be an EDN map {entity {attr val}}");
    }
    std::vector<Triple> triples;
    for (const auto& [entity, attmap] : pattern.as_map()) {
        if (attmap.type() != edn::Type::Map) {
            throw std::runtime_error("query: entity must map to an attr map");
        }
        for (const auto& [attr, value] : attmap.as_map()) {
            if (attr.is_symbol()) {
                throw std::runtime_error("query: variable attributes are unsupported");
            }
            // fully ground triples are kept: execute() verifies them against
            // the db, so a literal entity+value acts as an existence check
            triples.push_back({entity, attr, value});
        }
    }
    return triples;
}

Value result_to_value(const Result& r) {
    edn::MapPairs out;
    for (const auto& [e, am] : r) {
        edn::MapPairs inner;
        for (const auto& [a, v] : am) inner.emplace_back(a, v);
        out.emplace_back(e, Value::map(std::move(inner)));
    }
    return Value::map(std::move(out));
}

}  // namespace

Value query(const Db& db, const Value& pattern) {
    auto triples = parse_triples(pattern);
    auto r = execute(db, std::move(triples), Result{}, Pending{}, Bindings{});
    return result_to_value(r.value_or(Result{}));
}

Value query(const Db& db, const char* pattern_edn) {
    return query(db, edn::read_string(pattern_edn));
}

}  // namespace engine
