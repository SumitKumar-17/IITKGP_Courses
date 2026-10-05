#include "driver_main.h"

// The RCU store: lock-free reads, one background writer.
int main(int argc, char** argv) {
    return driver_main(argc, argv, "rcu", true);
}
