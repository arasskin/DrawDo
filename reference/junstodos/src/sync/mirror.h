#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "../db/store.h"

// Client side of the mirror: negentropy reconciliation to find the
// difference, then explicit put/del ops to make the replica equal the
// master. Direction is always explicit — up (local storage is master) or
// down (server is master; used for restore).
//
// Transport delivers one protocol request and returns the response
// (see protocol.h). It may be an in-process call (tests) or HTTP.
// Throws std::runtime_error on transport or protocol failure.

namespace jsync {

using Transport = std::function<std::string(std::string_view request)>;

struct MirrorStats {
    int rounds = 0;            // negentropy round-trips
    size_t pushed = 0;         // records sent to the replica
    size_t deleted_remote = 0; // keys removed from the replica (up)
    size_t fetched = 0;        // records pulled from the server (down)
    size_t deleted_local = 0;  // local keys removed (down)
    bool converged = false;    // post-mirror fingerprints matched

    // phase timings in microseconds (filled by mirror_up)
    uint64_t snapshot_us = 0;   // load_all -> Vector + payload map
    uint64_t reconcile_us = 0;  // negentropy rounds incl. network
    uint64_t ops_us = 0;        // OPS request incl. network + server commit
    uint64_t verify_us = 0;     // STAT round incl. network
    uint64_t total_us = 0;
};

// Make the server equal local storage. Refuses to wipe a non-empty server
// from an empty local store unless allow_wipe (the direction-confusion guard).
MirrorStats mirror_up(store::Store* s, const Transport& transport, bool allow_wipe = false);

// Make local storage equal the server (fresh install / explicit restore).
MirrorStats mirror_down(store::Store* s, const Transport& transport);

}  // namespace jsync
