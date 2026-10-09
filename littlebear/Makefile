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
TESTS := $(addprefix $(BUILD)/tests/,$(TEST_NAMES))
SANITIZER_TESTS := $(addprefix $(BUILD)/sanitize/,$(TEST_NAMES))

.PHONY: all server debug test sanitize clean
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

clean:
	rm -rf $(BUILD)
-include $(wildcard $(BUILD)/*.d $(BUILD)/tests/*.d $(BUILD)/sanitize/*.d)
