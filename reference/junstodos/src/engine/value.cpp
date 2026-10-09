#include "value.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace edn {

// ---- keyword/symbol interner ---------------------------------------------------

namespace {
struct Interner {
    std::mutex mu;
    std::vector<std::string> names;
    std::unordered_map<std::string, uint32_t> ids;

    uint32_t intern(std::string_view name) {
        std::lock_guard<std::mutex> lock(mu);
        auto it = ids.find(std::string(name));
        if (it != ids.end()) return it->second;
        uint32_t id = (uint32_t)names.size();
        names.emplace_back(name);
        ids.emplace(names.back(), id);
        return id;
    }
    std::string lookup(uint32_t id) {
        std::lock_guard<std::mutex> lock(mu);
        return names[id];
    }
};
Interner& interner() {
    static Interner g;
    return g;
}

size_t hash_mix(size_t h, size_t x) {
    // FNV-ish combine
    h ^= x + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}
}  // namespace

// ---- construction ---------------------------------------------------------------

Value::Value(std::string s) : v_(std::make_shared<const std::string>(std::move(s))) {}

Value Value::string(std::string s) { return Value(std::move(s)); }

Value Value::keyword(std::string_view name) {
    Value v;
    v.v_ = Kw{interner().intern(name)};
    return v;
}

Value Value::symbol(std::string_view name) {
    Value v;
    v.v_ = Sym{interner().intern(name)};
    return v;
}

Value Value::vector(Vec elems) {
    Value v;
    v.v_ = std::make_shared<const Vec>(std::move(elems));
    return v;
}

Value Value::map(MapPairs pairs) {
    // canonicalize: sort by printed key; later duplicates win
    std::stable_sort(pairs.begin(), pairs.end(),
                     [](const auto& a, const auto& b) { return pr_str(a.first) < pr_str(b.first); });
    MapPairs dedup;
    for (auto& p : pairs) {
        if (!dedup.empty() && dedup.back().first == p.first) {
            dedup.back().second = std::move(p.second);
        } else {
            dedup.push_back(std::move(p));
        }
    }
    Value v;
    v.v_ = std::make_shared<const MapPairs>(std::move(dedup));
    return v;
}

// ---- accessors -------------------------------------------------------------------

Type Value::type() const {
    switch (v_.index()) {
        case 0: return Type::Nil;
        case 1: return Type::Bool;
        case 2: return Type::Int;
        case 3: return Type::Double;
        case 4: return Type::String;
        case 5: return Type::Keyword;
        case 6: return Type::Symbol;
        case 7: return Type::Vector;
        default: return Type::Map;
    }
}

const std::string& Value::as_string() const {
    return *std::get<std::shared_ptr<const std::string>>(v_);
}

std::string Value::name() const {
    if (auto* k = std::get_if<Kw>(&v_)) return interner().lookup(k->id);
    if (auto* s = std::get_if<Sym>(&v_)) return interner().lookup(s->id);
    throw std::runtime_error("edn: name() on non-keyword/symbol");
}

const Vec& Value::as_vector() const { return *std::get<std::shared_ptr<const Vec>>(v_); }
const MapPairs& Value::as_map() const { return *std::get<std::shared_ptr<const MapPairs>>(v_); }

Value Value::get(const Value& key) const {
    if (type() != Type::Map) return Value();
    for (const auto& [k, v] : as_map()) {
        if (k == key) return v;
    }
    return Value();
}

// ---- equality / hashing -----------------------------------------------------------

bool Value::operator==(const Value& o) const {
    if (v_.index() != o.v_.index()) return false;
    switch (type()) {
        case Type::Nil: return true;
        case Type::Bool: return as_bool() == o.as_bool();
        case Type::Int: return as_int() == o.as_int();
        case Type::Double: return as_double() == o.as_double();
        case Type::String: return as_string() == o.as_string();
        case Type::Keyword: return std::get<Kw>(v_).id == std::get<Kw>(o.v_).id;
        case Type::Symbol: return std::get<Sym>(v_).id == std::get<Sym>(o.v_).id;
        case Type::Vector: return as_vector() == o.as_vector();
        case Type::Map: return as_map() == o.as_map();  // canonical order makes this valid
    }
    return false;
}

size_t Value::hash() const {
    size_t h = (size_t)type() * 0x100000001b3ull;
    switch (type()) {
        case Type::Nil: break;
        case Type::Bool: h = hash_mix(h, as_bool() ? 1 : 0); break;
        case Type::Int: h = hash_mix(h, (size_t)as_int()); break;
        case Type::Double: {
            double d = as_double();
            size_t bits;
            static_assert(sizeof bits == sizeof d);
            memcpy(&bits, &d, sizeof d);
            h = hash_mix(h, bits);
            break;
        }
        case Type::String: h = hash_mix(h, std::hash<std::string>{}(as_string())); break;
        case Type::Keyword: h = hash_mix(h, std::get<Kw>(v_).id); break;
        case Type::Symbol: h = hash_mix(h, 0xABCD ^ std::get<Sym>(v_).id); break;
        case Type::Vector:
            for (const auto& e : as_vector()) h = hash_mix(h, e.hash());
            break;
        case Type::Map:
            for (const auto& [k, v] : as_map()) h = hash_mix(hash_mix(h, k.hash()), v.hash());
            break;
    }
    return h;
}

// ---- printer -----------------------------------------------------------------------

static void print_to(std::string& out, const Value& v) {
    switch (v.type()) {
        case Type::Nil: out += "nil"; break;
        case Type::Bool: out += v.as_bool() ? "true" : "false"; break;
        case Type::Int: out += std::to_string(v.as_int()); break;
        case Type::Double: {
            char buf[32];
            snprintf(buf, sizeof buf, "%.17g", v.as_double());
            out += buf;
            if (out.find_first_of(".eE", out.size() - strlen(buf)) == std::string::npos) {
                out += ".0";  // keep doubles distinguishable from ints
            }
            break;
        }
        case Type::String: {
            out += '"';
            for (char c : v.as_string()) {
                switch (c) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    case '\r': out += "\\r"; break;
                    default: out += c;
                }
            }
            out += '"';
            break;
        }
        case Type::Keyword: out += ':'; out += v.name(); break;
        case Type::Symbol: out += v.name(); break;
        case Type::Vector: {
            out += '[';
            bool first = true;
            for (const auto& e : v.as_vector()) {
                if (!first) out += ' ';
                first = false;
                print_to(out, e);
            }
            out += ']';
            break;
        }
        case Type::Map: {
            out += '{';
            bool first = true;
            for (const auto& [k, val] : v.as_map()) {
                if (!first) out += ' ';
                first = false;
                print_to(out, k);
                out += ' ';
                print_to(out, val);
            }
            out += '}';
            break;
        }
    }
}

std::string pr_str(const Value& v) {
    std::string out;
    print_to(out, v);
    return out;
}

// ---- reader ------------------------------------------------------------------------

namespace {
struct Reader {
    std::string_view in;
    size_t pos = 0;

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("edn read error at " + std::to_string(pos) + ": " + msg);
    }
    bool eof() const { return pos >= in.size(); }
    char peek() const { return in[pos]; }

    void skip_ws() {
        while (!eof()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
                pos++;
            } else if (c == ';') {  // comment to end of line
                while (!eof() && peek() != '\n') pos++;
            } else {
                break;
            }
        }
    }

    static bool is_delim(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' ||
               c == '[' || c == ']' || c == '{' || c == '}' || c == '(' || c == ')' ||
               c == '"' || c == ';';
    }

    Value read() {
        skip_ws();
        if (eof()) fail("unexpected end of input");
        char c = peek();
        if (c == '[') return read_vector();
        if (c == '{') return read_map();
        if (c == '"') return read_str();
        if (c == ':') return read_keyword();
        if (c == '-' || c == '+' || isdigit((unsigned char)c)) {
            // could be a number or a symbol like '-'; try number first
            size_t save = pos;
            if (auto num = try_read_number()) return *num;
            pos = save;
        }
        return read_symbolish();
    }

    Value read_vector() {
        pos++;  // [
        Vec elems;
        for (;;) {
            skip_ws();
            if (eof()) fail("unterminated vector");
            if (peek() == ']') { pos++; return Value::vector(std::move(elems)); }
            elems.push_back(read());
        }
    }

    Value read_map() {
        pos++;  // {
        MapPairs pairs;
        for (;;) {
            skip_ws();
            if (eof()) fail("unterminated map");
            if (peek() == '}') { pos++; return Value::map(std::move(pairs)); }
            Value k = read();
            skip_ws();
            if (eof() || peek() == '}') fail("map with odd number of forms");
            Value v = read();
            pairs.emplace_back(std::move(k), std::move(v));
        }
    }

    Value read_str() {
        pos++;  // "
        std::string s;
        for (;;) {
            if (eof()) fail("unterminated string");
            char c = in[pos++];
            if (c == '"') return Value::string(std::move(s));
            if (c == '\\') {
                if (eof()) fail("unterminated escape");
                char e = in[pos++];
                switch (e) {
                    case '"': s += '"'; break;
                    case '\\': s += '\\'; break;
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    default: fail(std::string("unsupported escape \\") + e);
                }
            } else {
                s += c;
            }
        }
    }

    std::string read_token() {
        size_t start = pos;
        while (!eof() && !is_delim(peek())) pos++;
        if (pos == start) fail("empty token");
        return std::string(in.substr(start, pos - start));
    }

    Value read_keyword() {
        pos++;  // :
        return Value::keyword(read_token());
    }

    std::optional<Value> try_read_number() {
        size_t start = pos;
        if (peek() == '-' || peek() == '+') pos++;
        if (eof() || !isdigit((unsigned char)peek())) return std::nullopt;
        bool floating = false;
        while (!eof() && !is_delim(peek())) {
            char c = peek();
            if (isdigit((unsigned char)c)) { pos++; }
            else if (c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') { floating = true; pos++; }
            else return std::nullopt;  // e.g. "1abc" -> not a number
        }
        std::string tok(in.substr(start, pos - start));
        try {
            if (floating) return Value::dbl(std::stod(tok));
            return Value::integer((int64_t)std::stoll(tok));
        } catch (...) {
            return std::nullopt;
        }
    }

    Value read_symbolish() {
        std::string tok = read_token();
        if (tok == "nil") return Value::nil();
        if (tok == "true") return Value::boolean(true);
        if (tok == "false") return Value::boolean(false);
        return Value::symbol(tok);
    }
};
}  // namespace

Value read_string(std::string_view edn) {
    Reader r{edn};
    Value v = r.read();
    r.skip_ws();
    if (!r.eof()) r.fail("trailing input");
    return v;
}

}  // namespace edn
