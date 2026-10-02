#include "../LoadFifo.h"
LoadFifo top;
#ifndef SYNTHESIS
#define TEST_TOP LoadFifo
#include "FifoCases.h"

int main() {
    try {
        fifo_cases(false);
        std::cout << "LoadFifo PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
