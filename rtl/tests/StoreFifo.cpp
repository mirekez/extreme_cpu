#include "../StoreFifo.h"
StoreFifo top;
#ifndef SYNTHESIS
#define TEST_TOP StoreFifo
#define TEST_STORE_FIFO
#include "FifoCases.h"

int main() {
    try {
        fifo_cases(true);
        std::cout << "StoreFifo PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
