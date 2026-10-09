#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../db/store.h"

// Wire protocol for the storage <-> server mirror. Request/response pairs;
// the server is stateless across messages (each negentropy round carries its
// own state). All integers big-endian.
//
// Request  = [u8 type][body]
//   NEG   1: body = raw negentropy message        -> resp: raw negentropy message
//   OPS   2: body = puts + dels (mirror ops)      -> resp: [u8 1]
//   STAT  3: body empty                           -> resp: [u64 count][16B fingerprint]
//   FETCH 4: body = keys                          -> resp: records
//
// Version ids (the 32-byte negentropy id): [updated_at BE64 | 24-byte entity
// uuid]. Every edit mints a new version id — matching negentropy's model of
// immutable items — and any id maps arithmetically back to its store key,
// so no id->key index is needed on either side.

namespace jsync {

enum MsgType : uint8_t { NEG = 1, OPS = 2, STAT = 3, FETCH = 4 };

struct Key {
    uint64_t updated_at = 0;
    store::Id id{};
};

struct Record {
    uint64_t updated_at = 0;
    store::Id id{};
    std::string payload;
};

struct Stat {
    uint64_t count = 0;
    std::string fingerprint;  // 16 bytes
};

// version id helpers
store::Id make_version_id(uint64_t updated_at, const uint8_t uuid24[24]);
uint64_t version_ts(const store::Id& id);
store::Id id_from_sv(std::string_view id32);  // throws unless exactly 32 bytes

// requests
std::string encode_neg_req(std::string_view neg_msg);
std::string encode_ops_req(const std::vector<Record>& puts, const std::vector<Key>& dels);
std::string encode_stat_req();
std::string encode_fetch_req(const std::vector<Key>& keys);

// request decoding (server side); all throw std::runtime_error on malformed input
uint8_t req_type(std::string_view req);
std::string_view decode_neg_req(std::string_view req);
void decode_ops_req(std::string_view req, std::vector<Record>* puts, std::vector<Key>* dels);
std::vector<Key> decode_fetch_req(std::string_view req);

// responses
std::string encode_stat_resp(uint64_t count, std::string_view fingerprint16);
Stat decode_stat_resp(std::string_view resp);
std::string encode_records_resp(const std::vector<Record>& recs);
std::vector<Record> decode_records_resp(std::string_view resp);

}  // namespace jsync
