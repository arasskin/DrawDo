CXX = clang++
CPPFLAGS += -Iinclude
CPPFLAGS += -I../third_party/unordered_dense/include
CPPFLAGS += -Ibenchmarks -Ibuild/generated -Ibuild/reference/negentropy/cpp
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread
HEADERS := $(wildcard include/datafriend/*.h benchmarks/*.h)
BACKEND_HEADERS := $(wildcard ../littlebear-benchmarks/src/*.h ../littlebear-benchmarks/benchmarks/reference/*.h ../third_party/xxhash/*.h)
TEST_NAMES := tree_test reconciliation_test slab_test persistent_tree_test sorted_dense_test
TESTS := $(addprefix build/tests/,$(TEST_NAMES))
SANITIZERS := $(addprefix build/sanitize/,$(TEST_NAMES))
.DEFAULT_GOAL := test
.PHONY: test sanitize undefined benchmark benchmark-btrees benchmark-allocators clean

build/generated/btree_xor_core.h: benchmarks/generate_btree.py build/reference/negentropy/.ready
	python3 benchmarks/generate_btree.py

build/tests/%: tests/%.cpp $(HEADERS) $(BACKEND_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O2 -g $< -o $@ $(LDLIBS)

build/sanitize/%: tests/%.cpp $(HEADERS) $(BACKEND_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $< -o $@ $(LDLIBS)

build/undefined/%: tests/%.cpp $(HEADERS) $(BACKEND_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O1 -g -fsanitize=undefined -fno-omit-frame-pointer $< -o $@ $(LDLIBS)

build/compare: benchmarks/compare.cpp $(HEADERS) $(BACKEND_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -O3 -DNDEBUG $< -o $@ $(LDLIBS)

test: $(TESTS)
	@set -e; for test in $(TESTS); do "$$test"; done

sanitize: $(SANITIZERS)
	python3 tests/run_sanitizers.py $(SANITIZERS)

undefined: $(addprefix build/undefined/,$(TEST_NAMES))
	python3 tests/run_sanitizers.py $^

benchmark: build/compare
	python3 benchmarks/run.py

benchmark-btrees: build/compare
	python3 benchmarks/run_btrees.py

benchmark-allocators: build/compare
	python3 benchmarks/run_allocators.py

clean:
	rm -rf build

# The upstream B-tree is an optional measurement baseline, never production code.
.PHONY: reference test-btrees
build/reference/negentropy/.ready: tool/fetch_reference.py
	python3 tool/fetch_reference.py
reference: build/reference/negentropy/.ready
build/compare build/tests/btree_test build/sanitize/btree_test build/undefined/btree_test: build/generated/btree_xor_core.h
build/compare build/tests/btree_test build/sanitize/btree_test build/undefined/btree_test: CPPFLAGS += $(shell pkg-config --cflags libcrypto)
build/compare build/tests/btree_test build/sanitize/btree_test build/undefined/btree_test: LDLIBS += $(shell pkg-config --libs libcrypto)
test-btrees: build/tests/btree_test
	./build/tests/btree_test

# Portable storage/query engine and C ABI (desktop library for Dart tests).
.PHONY: native native-test
native:
	python3 tool/generate_attributes.py --check
	cmake -S . -B build/native -DCMAKE_BUILD_TYPE=Release
	cmake --build build/native -j 4
native-test: native
	ctest --test-dir build/native --output-on-failure
