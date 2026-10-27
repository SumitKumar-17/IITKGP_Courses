#include "driver_main.h"

// Built with -DNOVA_SYNC=1: one std::mutex around every put and get.
int main(int argc, char** argv) {
    return driver_main(argc, argv, "global_lock", true);
}
