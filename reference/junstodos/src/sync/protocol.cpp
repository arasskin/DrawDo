#include "protocol.h"

#include <cstring>
#include <stdexcept>

namespace jsync {

// ---- primitive writers/readers ---------------------------------------------

static void put_u8(std::string& out, uint8_t v) { out.push_back((char)v); }

static void put_u32(std::string& out, uint32_t v) {
    for (int i = 3; i >= 0; i--) out.push_back((char)((v >> (8 * i)) & 0xFF));
}

static void put_u64(std::string& out, uint64_t v) {
    for (int i = 7; i >= 0; i--) out.push_back((char)((v >> (8 * i)) & 0xFF));
}

struct Reader {
    std::string_view buf;
    size_t pos = 0;

    void need(size_t n) const {
        if (pos + n > buf.size()) throw std::runtime_error("sync: truncated message");
    }
    uint8_t u8() { need(1); return (uint8_t)buf[pos++]; }
    uint32_t u32() {
        need(4);
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v = (v << 8) | (uint8_t)buf[pos++];
        return v;
    }
    uint64_t u64() {
        need(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v = (v << 8) | (uint8_t)buf[pos++];
        return v;
    }
    std::string_view bytes(size_t n) {
        need(n);
        auto sv = buf.substr(pos, n);
        pos += n;
        return sv;
    }
    std::string_view rest() { auto sv = buf.substr(pos); pos = buf.size(); return sv; }
    void done() const {
        if (pos != buf.size()) throw std::runtime_error("sync: trailing bytes");
    }
};

static void put_key(std::string& out, const Key& k) {
    put_u64(out, k.updated_at);
    out.append((const char*)k.id.data(), store::ID_SIZE);
}

static Key read_key(Reader& r) {
    Key k;
    k.updated_at = r.u64();
    auto idb = r.bytes(store::ID_SIZE);
    memcpy(k.id.data(), idb.data(), store::ID_SIZE);
    return k;
}

// ---- version ids -------------------------------------------------------------

store::Id make_version_id(uint64_t updated_at, const uint8_t uuid24[24]) {
    store::Id id{};
    for (int i = 0; i < 8; i++) id[i] = (uint8_t)((updated_at >> (8 * (7 - i))) & 0xFF);
    memcpy(id.data() + 8, uuid24, 24);
    return id;
}

uint64_t version_ts(const store::Id& id) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | id[i];
    return v;
}

store::Id id_from_sv(std::string_view id32) {
    if (id32.size() != store::ID_SIZE) throw std::runtime_error("sync: bad id size");
    store::Id id;
    memcpy(id.data(), id32.data(), store::ID_SIZE);
    return id;
}

// ---- requests ----------------------------------------------------------------

std::string encode_neg_req(std::string_view neg_msg) {
    std::string out;
    put_u8(out, NEG);
    out.append(neg_msg);
    return out;
}

std::string encode_ops_req(const std::vector<Record>& puts, const std::vector<Key>& dels) {
    std::string out;
    put_u8(out, OPS);
    put_u32(out, (uint32_t)puts.size());
    for (const auto& p : puts) {
        put_key(out, Key{p.updated_at, p.id});
        put_u32(out, (uint32_t)p.payload.size());
        out.append(p.payload);
    }
    put_u32(out, (uint32_t)dels.size());
    for (const auto& d : dels) put_key(out, d);
    return out;
}

std::string encode_stat_req() {
    std::string out;
    put_u8(out, STAT);
    return out;
}

std::string encode_fetch_req(const std::vector<Key>& keys) {
    std::string out;
    put_u8(out, FETCH);
    put_u32(out, (uint32_t)keys.size());
    for (const auto& k : keys) put_key(out, k);
    return out;
}

uint8_t req_type(std::string_view req) {
    Reader r{req};
    return r.u8();
}

std::string_view decode_neg_req(std::string_view req) {
    Reader r{req};
    if (r.u8() != NEG) throw std::runtime_error("sync: not a NEG request");
    return r.rest();
}

void decode_ops_req(std::string_view req, std::vector<Record>* puts, std::vector<Key>* dels) {
    Reader r{req};
    if (r.u8() != OPS) throw std::runtime_error("sync: not an OPS request");
    uint32_t np = r.u32();
    for (uint32_t i = 0; i < np; i++) {
        Record rec;
        Key k = read_key(r);
        rec.updated_at = k.updated_at;
        rec.id = k.id;
        rec.payload = std::string(r.bytes(r.u32()));
        puts->push_back(std::move(rec));
    }
    uint32_t nd = r.u32();
    for (uint32_t i = 0; i < nd; i++) dels->push_back(read_key(r));
    r.done();
}

std::vector<Key> decode_fetch_req(std::string_view req) {
    Reader r{req};
    if (r.u8() != FETCH) throw std::runtime_error("sync: not a FETCH request");
    std::vector<Key> keys((size_t)0);
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) keys.push_back(read_key(r));
    r.done();
    return keys;
}

// ---- responses ---------------------------------------------------------------

std::string encode_stat_resp(uint64_t count, std::string_view fingerprint16) {
    if (fingerprint16.size() != 16) throw std::runtime_error("sync: bad fingerprint size");
    std::string out;
    put_u64(out, count);
    out.append(fingerprint16);
    return out;
}

Stat decode_stat_resp(std::string_view resp) {
    Reader r{resp};
    Stat st;
    st.count = r.u64();
    st.fingerprint = std::string(r.bytes(16));
    r.done();
    return st;
}

std::string encode_records_resp(const std::vector<Record>& recs) {
    std::string out;
    put_u32(out, (uint32_t)recs.size());
    for (const auto& rec : recs) {
        put_key(out, Key{rec.updated_at, rec.id});
        put_u32(out, (uint32_t)rec.payload.size());
        out.append(rec.payload);
    }
    return out;
}

std::vector<Record> decode_records_resp(std::string_view resp) {
    Reader r{resp};
    std::vector<Record> recs;
    uint32_t n = r.u32();
    for (uint32_t i = 0; i < n; i++) {
        Record rec;
        Key k = read_key(r);
        rec.updated_at = k.updated_at;
        rec.id = k.id;
        rec.payload = std::string(r.bytes(r.u32()));
        recs.push_back(std::move(rec));
    }
    r.done();
    return recs;
}

}  // namespace jsync
