#pragma once
#include <string>

#include "mirror.h"

// Network half of the transport seam. Returns a Transport that POSTs each
// protocol request to `url` (application/octet-stream) and returns the raw
// response body; throws std::runtime_error on network or HTTP failure.
//
// The returned Transport BLOCKS the calling thread for the round-trip —
// call it from a background thread, never from the frame callback.
//
// Platform implementations: net_ios.mm (NSURLSession). Android later.

namespace jsync {

Transport http_transport(std::string url, int timeout_seconds = 15);

}  // namespace jsync
