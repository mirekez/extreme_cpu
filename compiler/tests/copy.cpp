#include <stddef.h>
alignas(64) unsigned char source[256], dest[256];
volatile unsigned length = 137;

extern "C" unsigned kernel() {
    for (unsigned i = 0; i < 256; ++i) {
        source[i] = i * 5u + 7;
        dest[i] = 0xa5;
    }
    __builtin_memcpy(dest + 3, source + 3, length); // common alignment: prefix, COPY, tail
    for (unsigned i = 0; i < 256; ++i) {
        unsigned expected = (i >= 3 && i < 140) ? source[i] : 0xa5;
        if (dest[i] != expected) {
            return 1;
        }
    }
    __builtin_memcpy(dest + 1, source + 2, 71); // different alignment: byte fallback
    for (unsigned i = 0; i < 71; ++i) {
        if (dest[i + 1] != source[i + 2]) {
            return 2;
        }
    }
    return 42;
}
