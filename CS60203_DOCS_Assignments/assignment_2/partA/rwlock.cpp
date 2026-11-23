#include "driver_main.h"

// Built with -DNOVA_SYNC=2: std::shared_mutex, shared for get, unique for put.
int main(int argc, char** argv) {
    return driver_main(argc, argv, "rwlock", true);
}
