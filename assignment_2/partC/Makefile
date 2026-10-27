CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -Wall -Wextra -I. -I../common
BUILD    := build
COMMON   := ../common/workload_gen.cpp
SHARED   := ../common/driver_main.h ../common/workload_gen.h
STORE    := kvstore.cpp kvstore.h

all: $(BUILD)/kvstore_test $(BUILD)/stress_test $(BUILD)/txn_test $(BUILD)/rcu

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/kvstore_test: kvstore_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) kvstore_test.cpp kvstore.cpp -o $@

$(BUILD)/stress_test: stress_test.cpp $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) stress_test.cpp kvstore.cpp -o $@

$(BUILD)/txn_test: txn_test.cpp join.h ../common/schema.h $(STORE) | $(BUILD)
	$(CXX) $(CXXFLAGS) txn_test.cpp kvstore.cpp -o $@

$(BUILD)/rcu: rcu.cpp $(STORE) $(COMMON) $(SHARED) | $(BUILD)
	$(CXX) $(CXXFLAGS) rcu.cpp kvstore.cpp $(COMMON) -o $@

test: $(BUILD)/kvstore_test $(BUILD)/stress_test $(BUILD)/txn_test
	$(BUILD)/kvstore_test
	$(BUILD)/stress_test 6 5 64
	$(BUILD)/txn_test 3

# Memory errors and data races. Both would show up as use-after-free or a
# torn row if reclamation freed something a reader still held.
check: | $(BUILD)
	$(CXX) -std=c++17 -O1 -g -pthread -I. -I../common -fsanitize=address,undefined kvstore_test.cpp kvstore.cpp -o $(BUILD)/san_test
	$(BUILD)/san_test
	$(CXX) -std=c++17 -O1 -g -pthread -I. -I../common -fsanitize=address,undefined stress_test.cpp kvstore.cpp -o $(BUILD)/asan_stress
	$(BUILD)/asan_stress 6 5 8
	$(CXX) -std=c++17 -O1 -g -pthread -I. -I../common -fsanitize=thread stress_test.cpp kvstore.cpp -o $(BUILD)/tsan_stress
	$(BUILD)/tsan_stress 4 5 8
	$(CXX) -std=c++17 -O1 -g -pthread -I. -I../common -fsanitize=address,undefined txn_test.cpp kvstore.cpp -o $(BUILD)/asan_txn
	$(BUILD)/asan_txn 3
	$(CXX) -std=c++17 -O1 -g -pthread -I. -I../common -fsanitize=thread txn_test.cpp kvstore.cpp -o $(BUILD)/tsan_txn
	$(BUILD)/tsan_txn 3

clean:
	rm -rf $(BUILD)

.PHONY: all test check clean
