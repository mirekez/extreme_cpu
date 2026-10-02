#pragma once
#include <cpphdl.h>
#ifndef EC_BITS
#define EC_BITS 128
#endif
#ifndef EC_DEPTH
#define EC_DEPTH 16
#endif
#ifndef EC_CORES
#define EC_CORES 2
#endif
#ifndef EC_BANKS
#define EC_BANKS 2
#endif
#ifndef EC_BANK_WORDS
#define EC_BANK_WORDS 4096
#endif
#ifndef EC_REGS
#define EC_REGS 8
#endif
static_assert(EC_BITS >= 64 && EC_BITS <= 512 && (EC_BITS & (EC_BITS - 1)) == 0);
static_assert(EC_DEPTH >= 2 && EC_DEPTH <= 256 && (EC_DEPTH & (EC_DEPTH - 1)) == 0);
static_assert(EC_CORES >= 1 && EC_CORES <= 16 && EC_BANKS >= 1);
static_assert(EC_REGS >= 3 && EC_REGS <= 16);
static_assert(EC_BANK_WORDS > 0);
static_assert(uint64_t(EC_BANK_WORDS) * (EC_BITS / 8) * EC_BANKS <= (1ull << 32));
using namespace cpphdl;
