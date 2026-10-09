#pragma once
#include <functional>

#include "store.h"
#include "negentropy.h"
#include "negentropy/storage/SliceAELMDB.h"

// Sync-layer view of the store: exposes the dbi as a negentropy storage
// backend inside a read transaction. Deliberately a separate header so that
// store.h consumers (app, engine) never see lmdb/negentropy types — only the
// sync modules include this.
//
// Keep fn fast and local: it runs inside a live LMDB read txn, which pins
// old pages while open. Never perform network I/O inside fn.

namespace store {

void with_sync_view(Store* s,
                    const std::function<void(negentropy::storage::WholeAELMDB&)>& fn);

}  // namespace store
