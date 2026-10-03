#include <stdint.h>
#include <string.h>

alignas(EC_BUS_BYTES) unsigned char data[512];
volatile unsigned length_input = 137;

__attribute__((noinline)) bool move(unsigned destination, unsigned source, unsigned length) {
    for (unsigned i = 0; i < sizeof(data); ++i) {
        data[i] = (i * 37 + 13) & 255;
    }
    void* result = memmove(data + destination, data + source, length);
    if (result != data + destination) {
        return false;
    }
    for (unsigned i = 0; i < sizeof(data); ++i) {
        unsigned original =
            i >= destination && i < destination + length ? source + i - destination : i;
        if (data[i] != ((original * 37 + 13) & 255)) {
            return false;
        }
    }
    return true;
}

extern "C" unsigned kernel() {
    unsigned length = length_input;
    if (!move(3, 0, length) || !move(0, 3, length)) {
        return 1;
    }
    if (!move(17, 17, length) || !move(0, 256, 0)) {
        return 2;
    }
    if (!move(256, 1, length) || !move(1, 256, length)) {
        return 3;
    }
    return 42;
}
