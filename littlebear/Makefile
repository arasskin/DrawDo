CXX = g++
CPPFLAGS += -Isrc
CXXFLAGS = -std=c++23 -Wall -Wextra $(NIX_CFLAGS_COMPILE)
LDFLAGS ?=
LDLIBS = -luring
HEADERS := $(wildcard src/*.h src/main_data_containers/*.h)
TEST_NAMES := size_class_test arenas_test tree_test hash_table_test
TESTS := $(addprefix build/tests/,$(TEST_NAMES))
SANITIZER_TESTS := $(addprefix build/sanitize/,$(TEST_NAMES))
# Keep sanitizer bookkeeping practical on the 500 MB target; checks stay enabled.
ASAN_OPTIONS ?= quarantine_size_mb=16:thread_local_quarantine_size_kb=64

.PHONY: all debug test sanitize benchmark benchmark-table clean
all: core

core: src/core.cpp $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS) $(LDLIBS)

debug: build/core-debug

build/core-debug: src/core.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $< $(LDFLAGS) $(LDLIBS)

build/tests/%: tests/%.cpp tests/check.h $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O2 -g -o $@ $< $(LDFLAGS)

build/sanitize/%: tests/%.cpp tests/check.h $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $< $(LDFLAGS)

test: $(TESTS)
	@set -e; for test in $(TESTS); do "$$test"; done

sanitize: $(SANITIZER_TESTS)
	@set -e; for test in $(SANITIZER_TESTS); do ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS=halt_on_error=1 "$$test"; done

build/allocator-benchmark: benchmarks/allocator.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS)

benchmark: build/allocator-benchmark
	./build/allocator-benchmark

build/hash-table-benchmark: benchmarks/hash_table.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS)

benchmark-table: build/hash-table-benchmark
	./build/hash-table-benchmark

clean:
	rm -rf core build
