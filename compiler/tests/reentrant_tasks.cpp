#include <tasks/tasks.h>

__attribute__((noinline)) unsigned sum(unsigned depth, unsigned seed) {
    volatile unsigned saved = seed;
    if (depth == 0) {
        return saved;
    }
    unsigned child = sum(depth - 1, seed + 1);
    return child + saved;
}

using Calculation = unsigned (*)(unsigned, unsigned);
Calculation volatile calculate = sum;

void worker() {
    unsigned seed = 10 * (extreme::tasks::id() + 1);
    extreme::tasks::finish(calculate(2, seed));
}

extern "C" unsigned kernel() {
    using namespace extreme::tasks;
    if (issue(0, worker) != ok || issue(1, worker) != ok) {
        return 1;
    }
    // Boot and both workers call the same recursive function with private locals.
    if (calculate(2, 30) != 93) {
        return 2;
    }
    for (unsigned id = 0; id < 2; ++id) {
        unsigned result = 0;
        unsigned status;
        do {
            status = read(id, result);
        } while (status == not_ready);
        if (status != ok || result != 33 + id * 30) {
            return 3;
        }
    }
    return 42;
}
