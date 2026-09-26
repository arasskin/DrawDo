#pragma once
#include <cstddef>

namespace config {

constexpr size_t MAX_CONCURRENT_CONNECTIONS = 64;
constexpr int PUBLIC_PORT = 7777;
constexpr bool DEBUG = true;
constexpr const char *VAUES_FILE_NAME = "littlebear.values";
// Disposable cache extent; leave disk space for the OS, releases, and logs.
constexpr size_t VALUES_FILE_SIZE = 1ULL * 1024 * 1024 * 1024;


}
