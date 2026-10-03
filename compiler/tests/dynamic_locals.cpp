volatile unsigned length = 8193;
volatile unsigned observed_address;

__attribute__((noinline)) unsigned exercise(unsigned count) {
    unsigned sum = 0;
    for (unsigned round = 0; round < 12; ++round) {
        alignas(64) volatile unsigned char local[count];
        observed_address = reinterpret_cast<unsigned>(local);
        if (observed_address & 63) {
            return 1;
        }
        local[0] = round;
        local[count - 1] = round + 1;
        sum += local[0] + local[count - 1];
    }
    // More than 64 KiB was allocated across iterations. Leaving each scope
    // must restore the allocation cursor instead of exhausting the arena.
    return sum == 144 ? 42 : 2;
}

extern "C" unsigned kernel() {
    if (exercise(length) != 42) {
        return 3;
    }
    return exercise(length + 1);
}
