#include "driver_main.h"

// No synchronisation, so no writer thread and a single reader.
int main(int argc, char** argv) {
    return driver_main(argc, argv, "single_thread", false);
}
