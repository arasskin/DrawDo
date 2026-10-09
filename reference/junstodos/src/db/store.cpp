#include "store.h"
#include "store_sync.h"

#include <cstring>
#include <filesystem>
#include <stdexcept>

#include "lmdbxx/lmdb++.h"

namespace store {

struct Store {
    lmdb::env env{nullptr};
    lmdb::dbi dbi{0};  // opened once, post-schema; see note in open()
};

// ---- key encoding -----------------------------------------------------------

static constexpr size_t KEY_SIZE = 8 + ID_SIZE;

static std::string make_key(uint64_t updated_at, const Id& id) {
    std::string k(KEY_SIZE, '\0');
    for (int i = 0; i < 8; i++) k[i] = (char)((updated_at >> (8 * (7 - i))) & 0xFF);
    memcpy(&k[8], id.data(), ID_SIZE);
    return k;
}

static uint64_t key_ts(std::string_view k) {
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x = (x << 8) | (uint8_t)k[i];
    return x;
}

// ---- lifecycle --------------------------------------------------------------

Store* open(const std::string& dir, size_t mapsize_bytes) {
    std::filesystem::create_directories(dir);

    auto s = new Store();
    s->env = lmdb::env::create();
    s->env.set_max_dbs(8);
    s->env.set_mapsize(mapsize_bytes);
    s->env.open(dir.c_str(), 0);

    // Schema setup in its own committed txn. set_hash_offset is only legal on
    // an empty dbi; on an already-initialized store the offset reads back 8
    // and this block changes nothing.
    {
        auto txn = lmdb::txn::begin(s->env);
        auto dbi = lmdb::dbi::open(txn, "items",
            MDB_CREATE | MDB_AGG_ENTRIES | MDB_AGG_HASHSUM | MDB_AGG_HASHSOURCE_FROM_KEY);
        int off = 0;
        const int rc = ::mdb_get_hash_offset(txn, dbi.handle(), &off);
        if (rc != MDB_SUCCESS) lmdb::error::raise("mdb_get_hash_offset", rc);
        if (off != 8) {
            dbi.set_hash_offset(txn, 8);  // throws if dbi is non-empty
        }
        txn.commit();
    }
    // Open the working dbi handle exactly once, AFTER the schema txn commits.
    // Two reasons: (1) the AELMDB metadata wart only bites handles opened
    // before set_hash_offset ran — a post-schema handle reads offset 8; and
    // (2) mdb_dbi_open is documented non-concurrent, so calling it per-txn
    // would be a race once multiple threads transact on one Store.
    {
        auto txn = lmdb::txn::begin(s->env, nullptr, MDB_RDONLY);
        s->dbi = lmdb::dbi::open(txn, "items");
        txn.abort();  // dbi handles outlive their opening txn
    }
    return s;
}

void close(Store* s) {
    if (!s) return;
    s->env.close();
    delete s;
}

// ---- writes -----------------------------------------------------------------

struct WriterImpl {
    lmdb::txn& txn;
    lmdb::dbi dbi;
};

void Writer::put(uint64_t updated_at, const Id& id, std::string_view payload) {
    impl_->dbi.put(impl_->txn, std::string_view(make_key(updated_at, id)), payload);
}

void Writer::del(uint64_t updated_at, const Id& id) {
    impl_->dbi.del(impl_->txn, std::string_view(make_key(updated_at, id)));
}

void write(Store* s, const std::function<void(Writer&)>& fn) {
    auto txn = lmdb::txn::begin(s->env);
    WriterImpl impl{txn, s->dbi};
    Writer w(&impl);
    fn(w);
    txn.commit();  // txn dtor aborts if fn threw
}

// ---- reads ------------------------------------------------------------------

void load_all(Store* s, const std::function<void(const Item&)>& cb) {
    auto txn = lmdb::txn::begin(s->env, nullptr, MDB_RDONLY);
    auto cursor = lmdb::cursor::open(txn, s->dbi);
    std::string_view k, v;
    while (cursor.get(k, v, MDB_NEXT)) {
        if (k.size() != KEY_SIZE) {
            throw std::runtime_error("store: corrupt key during load_all");
        }
        Item it;
        it.updated_at = key_ts(k);
        memcpy(it.id.data(), k.data() + 8, ID_SIZE);
        it.payload.assign(v.data(), v.size());
        cb(it);
    }
}

bool get(Store* s, uint64_t updated_at, const Id& id, std::string* payload_out) {
    auto txn = lmdb::txn::begin(s->env, nullptr, MDB_RDONLY);
    std::string_view v;
    if (!s->dbi.get(txn, std::string_view(make_key(updated_at, id)), v)) return false;
    if (payload_out) payload_out->assign(v.data(), v.size());
    return true;
}

uint64_t item_count(Store* s) {
    auto txn = lmdb::txn::begin(s->env, nullptr, MDB_RDONLY);
    return s->dbi.size(txn);
}

void with_sync_view(Store* s,
                    const std::function<void(negentropy::storage::WholeAELMDB&)>& fn) {
    auto txn = lmdb::txn::begin(s->env, nullptr, MDB_RDONLY);
    negentropy::storage::WholeAELMDB view(txn, s->dbi);
    fn(view);
}

}  // namespace store
