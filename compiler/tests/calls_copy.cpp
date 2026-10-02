alignas(64) unsigned char source[192], dest[192];
volatile unsigned input = 7;

__attribute__((noinline)) unsigned copy_inner(unsigned x) {
    __builtin_memcpy(dest, source, 192);
    return x + 9;
}

__attribute__((noinline)) unsigned copy_outer(unsigned x) {
    unsigned saved = x * 3;
    return copy_inner(x) + saved;
}

extern "C" unsigned kernel() {
    for (unsigned i = 0; i < 192; ++i) {
        source[i] = i * 7u + 5;
    }
    unsigned value = copy_outer(input);
    for (unsigned i = 0; i < 192; ++i) {
        if (source[i] != dest[i]) {
            return 1;
        }
    }
    return value;
}
