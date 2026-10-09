CXX = g++
BUILD ?= build
CPPFLAGS += -Isrc -Itests
CXXFLAGS = -std=c++23 -Wall -Wextra $(NIX_CFLAGS_COMPILE)
LDFLAGS ?=
ASAN_OPTIONS ?= quarantine_size_mb=16:thread_local_quarantine_size_kb=64
SANITIZERS = -fsanitize=address,undefined -fno-omit-frame-pointer
# Compiler-generated dependencies rebuild only affected targets, including shared headers.
DEPFLAGS = -MMD -MP
TEST_NAMES := size_class_test wide_tree_test hash_table_test storage_test reconciliation_test index_sync_test
REFERENCE_NAMES := reference/tree_test reference/arena_test
TESTS := $(addprefix $(BUILD)/tests/,$(TEST_NAMES) $(REFERENCE_NAMES))
SANITIZER_TESTS := $(addprefix $(BUILD)/sanitize/,$(TEST_NAMES) $(REFERENCE_NAMES))

.PHONY: all server debug test sanitize benchmark benchmark-table clean
all: server
server: $(BUILD)/server
$(BUILD)/server: src/server.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS) -luring -lsodium

debug: $(BUILD)/server-debug
$(BUILD)/server-debug: src/server.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O1 -g $(SANITIZERS) -o $@ $< $(LDFLAGS) -luring -lsodium

$(BUILD)/tests/%: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O2 -g -o $@ $< $(LDFLAGS) $(TEST_LIBS)
$(BUILD)/sanitize/%: tests/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O1 -g $(SANITIZERS) -o $@ $< $(LDFLAGS) $(TEST_LIBS)
ifeq ($(shell uname -s),Linux)
$(BUILD)/tests/storage_test $(BUILD)/sanitize/storage_test: TEST_LIBS = -luring
endif

test: $(TESTS)
	@set -e; for test in $(TESTS); do "$$test"; done
sanitize: $(SANITIZER_TESTS)
	@set -e; for test in $(SANITIZER_TESTS); do ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS=halt_on_error=1 "$$test"; done

$(BUILD)/allocator-benchmark: benchmarks/allocator.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS)
benchmark: $(BUILD)/allocator-benchmark
	./$(BUILD)/allocator-benchmark
$(BUILD)/hash-table-benchmark: benchmarks/hash_table.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS)
benchmark-table: $(BUILD)/hash-table-benchmark
	./$(BUILD)/hash-table-benchmark

.PHONY: test-reconciliation sanitize-reconciliation benchmark-reconciliation benchmark-reconciliation-reference
test-reconciliation: $(BUILD)/tests/reconciliation_test $(BUILD)/tests/index_sync_test
	./$(BUILD)/tests/reconciliation_test
	./$(BUILD)/tests/index_sync_test
sanitize-reconciliation: $(BUILD)/sanitize/reconciliation_test $(BUILD)/sanitize/index_sync_test
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/sanitize/reconciliation_test
	ASAN_OPTIONS="$(ASAN_OPTIONS)" UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD)/sanitize/index_sync_test
$(BUILD)/reconciliation-benchmark: benchmarks/reconciliation.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -O3 -DNDEBUG -o $@ $< $(LDFLAGS)
benchmark-reconciliation: $(BUILD)/reconciliation-benchmark
	./$(BUILD)/reconciliation-benchmark
# Optional historical comparison; normal builds do not fetch or link libcrypto.
REFERENCE = ../datafriend-benchmarks/build/reference/negentropy
$(REFERENCE)/.ready: ../datafriend-benchmarks/tool/fetch_reference.py
	python3 ../datafriend-benchmarks/tool/fetch_reference.py
$(BUILD)/reconciliation-reference-benchmark: benchmarks/reconciliation_reference.cpp $(REFERENCE)/.ready
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -I$(REFERENCE)/cpp -O3 -DNDEBUG $< $$(pkg-config --cflags --libs libcrypto) -o $@
benchmark-reconciliation-reference: $(BUILD)/reconciliation-reference-benchmark
	./$(BUILD)/reconciliation-reference-benchmark

clean:
	rm -rf $(BUILD)
-include $(wildcard $(BUILD)/*.d $(BUILD)/tests/*.d $(BUILD)/tests/reference/*.d $(BUILD)/sanitize/*.d $(BUILD)/sanitize/reference/*.d)
