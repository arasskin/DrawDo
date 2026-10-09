#pragma once
#include <cstdint>

#include "db.h"
#include "../db/store.h"

// The glue between the datafriend engine (memory, source of truth) and the
// store/sync stack. Conventions (blessed 2026-07-10):
//   - entity ids are random 24-byte uuids, represented in the Db as their
//     48-char hex Value::string
//   - every live entity carries :updated-at (unix ms); glue::transact bumps
//     it monotonically on touched entities
//   - version id = [updated-at BE64 | uuid24]; payload = pr_str of the attmap
//   - deleting an entity = retracting all its attributes (empty attmap ==
//     absent as far as sync is concerned)

namespace glue {

edn::Value new_entity_id();
uint64_t now_ms();

// entity's :updated-at, or 0 when absent
uint64_t entity_updated_at(const engine::Db& db, const edn::Value& entity);

// engine::transact plus the :updated-at bump on every touched entity
engine::Db transact(engine::Db db, engine::Diff diff);

// pattern form: {entity {attr val} ...}, same literal shape query reads.
// Must be fully ground (no symbols anywhere) — build it with M/kw, placing
// runtime values directly. Compiles to adds through the bump above; use
// Diff/retract_entity for retraction. The string overload is read_string
// convenience for fully constant patterns.
engine::Db transact(engine::Db db, const edn::Value& pattern);
engine::Db transact(engine::Db db, const char* pattern_edn);

// full deletion: retract every attribute of the entity (no bump)
engine::Db retract_entity(engine::Db db, const edn::Value& entity);

// storage -> memory, on startup (and for reading friend stores into a Db)
engine::Db load(store::Store* s);

// memory -> storage: full-state comparison (closed loop — heals any drift,
// including payload changes). One atomic txn. Returns true if the store
// changed (i.e. a network mirror is worth scheduling).
bool sync_to_store(const engine::Db& db, store::Store* s);

}  // namespace glue
