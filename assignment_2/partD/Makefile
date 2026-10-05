CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -g -fno-omit-frame-pointer -pthread -Wall -Wextra -I../common
BUILD    := build

all: $(BUILD)/bench_rcu $(BUILD)/bench_rwlock $(BUILD)/bench_global $(BUILD)/bench_single

$(BUILD):
	mkdir -p $(BUILD)

# The final RCU store (Part C): point gets, joins, reclamation modes.
$(BUILD)/bench_rcu: bench.cpp ../partC/kvstore.cpp ../partC/kvstore.h ../partC/join.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../partC -DHAVE_RCU bench.cpp ../partC/kvstore.cpp -o $@

# The lock-based stores from Part A, for the copy-on-write comparison.
$(BUILD)/bench_rwlock: bench.cpp ../partA/kvstore.cpp ../partA/kvstore.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../partA -DNOVA_SYNC=2 bench.cpp ../partA/kvstore.cpp -o $@

$(BUILD)/bench_global: bench.cpp ../partA/kvstore.cpp ../partA/kvstore.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../partA -DNOVA_SYNC=1 bench.cpp ../partA/kvstore.cpp -o $@

$(BUILD)/bench_single: bench.cpp ../partA/kvstore.cpp ../partA/kvstore.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -I../partA -DNOVA_SYNC=0 bench.cpp ../partA/kvstore.cpp -o $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
