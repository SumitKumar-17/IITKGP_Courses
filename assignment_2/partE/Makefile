CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -msse4.2 -Wall -Wextra -I. -I../common
BUILD    := build
COMMON   := ../common/workload_gen.cpp
SHARED   := ../common/driver_main.h ../common/workload_gen.h
STORE    := kvstore.cpp wal.cpp kvstore.h wal.h

all: $(BUILD)/kvstore_test $(BUILD)/stress_test $(BUILD)/txn_test $(BUILD)/wal_test $(BUILD)/crash_test $(BUILD)/rcu

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kvstore_test: kvstore_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) kvstore_test.cpp kvstore.cpp wal.cpp -o $@

$(BUILD)/stress_test: stress_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) stress_test.cpp kvstore.cpp wal.cpp -o $@

$(BUILD)/txn_test: txn_test.cpp join.h ../common/schema.h $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) txn_test.cpp kvstore.cpp wal.cpp -o $@

$(BUILD)/wal_test: wal_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) wal_test.cpp kvstore.cpp wal.cpp -o $@

$(BUILD)/crash_test: crash_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) crash_test.cpp kvstore.cpp wal.cpp -o $@

$(BUILD)/rcu: rcu.cpp $(STORE) $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) rcu.cpp kvstore.cpp wal.cpp $(COMMON) -o $@

test: $(BUILD)/kvstore_test $(BUILD)/stress_test $(BUILD)/txn_test $(BUILD)/wal_test $(BUILD)/crash_test
	$(BUILD)/kvstore_test
	$(BUILD)/stress_test 6 5 64
	$(BUILD)/txn_test 3
	$(BUILD)/wal_test
	$(BUILD)/crash_test 40

# Memory errors and data races in the log and in the store around it.
check: | $(BUILD)
	$(CXX) -std=c++17 -O1 -g -pthread -msse4.2 -I. -I../common -fsanitize=address,undefined wal_test.cpp kvstore.cpp wal.cpp -o $(BUILD)/asan_wal
	$(BUILD)/asan_wal
	$(CXX) -std=c++17 -O1 -g -pthread -msse4.2 -I. -I../common -fsanitize=thread wal_test.cpp kvstore.cpp wal.cpp -o $(BUILD)/tsan_wal
	$(BUILD)/tsan_wal
	$(CXX) -std=c++17 -O1 -g -pthread -msse4.2 -I. -I../common -fsanitize=address,undefined txn_test.cpp kvstore.cpp wal.cpp -o $(BUILD)/asan_txn
	$(BUILD)/asan_txn 3
	$(CXX) -std=c++17 -O1 -g -pthread -msse4.2 -I. -I../common -fsanitize=address,undefined crash_test.cpp kvstore.cpp wal.cpp -o $(BUILD)/asan_crash
	$(BUILD)/asan_crash 12

clean:
	rm -rf $(BUILD)

.PHONY: all test check clean
