#!/bin/sh
# Run in a disposable Ubuntu container with this repo mounted read-only at /work.
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends g++ cmake make > /tmp/packages.log
cmake -S /work/datafriend -B /tmp/datafriend-build \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build /tmp/datafriend-build -j 1
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir /tmp/datafriend-build --output-on-failure --timeout "${DATAFRIEND_TEST_TIMEOUT:-180}" \
    --tests-regex "${DATAFRIEND_TEST_FILTER:-.}"
