CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -Wall -Wextra -I. -I../common
BUILD    := build

COMMON   := ../common/workload_gen.cpp
SHARED   := ../common/driver_main.h ../common/workload_gen.h

all: $(BUILD)/kvstore_test $(BUILD)/single_thread $(BUILD)/global_lock $(BUILD)/rwlock

# The same two lock drivers with the unaligned layout, for tools/layout_sensitivity.sh only.
ablation: $(BUILD)/global_lock_unaligned $(BUILD)/rwlock_unaligned

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kvstore_test: kvstore_test.cpp kvstore.cpp kvstore.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=0 kvstore_test.cpp kvstore.cpp -o $@

$(BUILD)/single_thread: single_thread.cpp kvstore.cpp kvstore.h $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=0 $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/global_lock: global_lock.cpp kvstore.cpp kvstore.h $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=1 $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/rwlock: rwlock.cpp kvstore.cpp kvstore.h $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=2 $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/global_lock_unaligned: global_lock.cpp kvstore.cpp kvstore.h $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=1 -DNOVA_NO_ALIGN $< kvstore.cpp $(COMMON) -o $@

$(BUILD)/rwlock_unaligned: rwlock.cpp kvstore.cpp kvstore.h $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) -DNOVA_SYNC=2 -DNOVA_NO_ALIGN $< kvstore.cpp $(COMMON) -o $@

test: $(BUILD)/kvstore_test
	$(BUILD)/kvstore_test

check: | $(BUILD)
	for s in 0 1 2; do \
	  $(CXX) -std=c++17 -O1 -g -fsanitize=address,undefined -DNOVA_SYNC=$$s kvstore_test.cpp kvstore.cpp -o $(BUILD)/san_test$$s && $(BUILD)/san_test$$s || exit 1; \
	done
	$(CXX) $(CXXFLAGS) -O1 -fsanitize=thread -DNOVA_SYNC=2 rwlock.cpp kvstore.cpp $(COMMON) -o $(BUILD)/tsan_rwlock
	$(BUILD)/tsan_rwlock --readers 4 --seconds 2 --writer-rate 20000

clean:
	rm -rf $(BUILD)

.PHONY: all ablation test check clean
