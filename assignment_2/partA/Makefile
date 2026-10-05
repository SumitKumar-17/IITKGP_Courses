CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -Wall -Wextra -I. -I../common
BUILD    := build

COMMON   := ../common/workload_gen.cpp

all: $(BUILD)/kvstore_test $(BUILD)/single_thread $(BUILD)/global_lock $(BUILD)/rwlock

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kvstore_test: kvstore_test.cpp kvstore.cpp kvstore.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=0 kvstore_test.cpp kvstore.cpp -o $@

$(BUILD)/single_thread: single_thread.cpp kvstore.cpp kvstore.h $(COMMON) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=0 $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/global_lock: global_lock.cpp kvstore.cpp kvstore.h $(COMMON) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=1 $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/rwlock: rwlock.cpp kvstore.cpp kvstore.h $(COMMON) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=2 $< kvstore.cpp $(COMMON) -o $@

test: $(BUILD)/kvstore_test
	$(BUILD)/kvstore_test

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
