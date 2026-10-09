#pragma once
#include <string>
#include <string_view>

#include "../db/store.h"

// Server side of the mirror protocol: one function, transport-agnostic.
// Wrap it in an HTTP handler (the real server) or call it directly
// (in-process tests). Stateless across requests; throws std::runtime_error
// on malformed input.

namespace jsync {

std::string sync_handle(store::Store* s, std::string_view request);

}  // namespace jsync
