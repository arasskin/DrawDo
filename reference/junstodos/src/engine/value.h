#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// Immutable EDN value type for the datafriend engine port. Values are cheap
// to copy (scalars inline, collections behind shared_ptr) and usable as keys
// in immer maps (hash + equality below).
//
// Supported subset: nil, booleans, 64-bit ints, doubles, strings, keywords,
// symbols, vectors, maps. Keywords and symbols are interned: comparison and
// hashing are O(1) on an id. EDN maps are canonicalized (pairs sorted by the
// printed key) so equality and hashing are order-insensitive, matching EDN
// semantics.

namespace edn {

enum class Type : uint8_t { Nil, Bool, Int, Double, String, Keyword, Symbol, Vector, Map };

class Value;
using Vec = std::vector<Value>;
using MapPairs = std::vector<std::pair<Value, Value>>;  // canonical order

class Value {
  public:
    Value() : v_(std::monostate{}) {}  // nil

    // implicit scalar construction, so patterns read like EDN literals:
    // M{{kw("text"), text}, {kw("done"), false}} (see M/kw/sym below)
    Value(bool b) : v_(b) {}
    Value(int i) : v_((int64_t)i) {}
    Value(int64_t i) : v_(i) {}
    Value(double d) : v_(d) {}
    Value(std::string s);
    Value(const char* s) : Value(std::string(s)) {}
    template <class T> Value(T*) = delete;  // block silent pointer -> bool

    static Value nil() { return Value(); }
    static Value boolean(bool b) { return Value(b); }
    static Value integer(int64_t i) { return Value(i); }
    static Value dbl(double d) { return Value(d); }
    static Value string(std::string s);
    static Value keyword(std::string_view name);  // without leading ':'
    static Value symbol(std::string_view name);
    static Value vector(Vec elems);
    static Value map(MapPairs pairs);  // canonicalizes; later duplicate keys win

    Type type() const;
    bool is_nil() const { return type() == Type::Nil; }
    bool is_symbol() const { return type() == Type::Symbol; }
    bool is_keyword() const { return type() == Type::Keyword; }

    bool as_bool() const { return std::get<bool>(v_); }
    int64_t as_int() const { return std::get<int64_t>(v_); }
    double as_double() const { return std::get<double>(v_); }
    const std::string& as_string() const;
    std::string name() const;  // keyword/symbol name (no ':')
    const Vec& as_vector() const;
    const MapPairs& as_map() const;

    // map lookup returning nil when absent (or when this isn't a map), so
    // reads chain: result.get(entity).get(kw("done"))
    Value get(const Value& key) const;

    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }
    size_t hash() const;

  private:
    struct Kw { uint32_t id; };
    struct Sym { uint32_t id; };

    std::variant<std::monostate, bool, int64_t, double,
                 std::shared_ptr<const std::string>,  // String
                 Kw, Sym,
                 std::shared_ptr<const Vec>,
                 std::shared_ptr<const MapPairs>>
        v_;
};

// literal construction vocabulary: patterns are built in place, runtime
// values slot in directly (this is the engine's unquote — no env, no parser):
//   query(db, M{{entity, M{{kw("done"), sym("?d")}}}})
inline Value kw(std::string_view name) { return Value::keyword(name); }
inline Value sym(std::string_view name) { return Value::symbol(name); }

struct M {
    Value v;
    M(std::initializer_list<std::pair<Value, Value>> kvs)
        : v(Value::map(MapPairs(kvs.begin(), kvs.end()))) {}
    operator Value() const& { return v; }
    operator Value() && { return std::move(v); }
};

struct ValueHash {
    size_t operator()(const Value& v) const { return v.hash(); }
};
struct ValueEq {
    bool operator()(const Value& a, const Value& b) const { return a == b; }
};

// EDN printer/reader for the supported subset. read throws std::runtime_error
// on malformed input or trailing garbage.
std::string pr_str(const Value& v);
Value read_string(std::string_view edn);

}  // namespace edn
