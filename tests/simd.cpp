#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "SystemHarness.h"
#include "../arch/instruction.h"
using namespace extreme;

struct SimdTest : Harness {
    static constexpr unsigned bytes = EC_BITS / 8;
    static constexpr unsigned dataBase = EC_CORES * 128;

    void reset() {
        run_in = false;
        host_mode_in = true;
        host_request_in = false;
        for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
            memory_enable_in[bank] = true;
        }
        tick(true);
        tick(true);
    }

    Word access(unsigned address, bool write, Word value = 0) {
        host_address_in = address;
        host_write_in = write;
        host_data_in = value;
        host_request_in = true;
        settle();
        require(OUT(host_ready_out), "SIMD host request not ready");
        tick();
        host_request_in = false;
        settle();
        require(OUT(host_response_out), "SIMD host response missing");
        Word result = read_word(OUT(host_result_out));
        tick();
        return result;
    }

    void program(unsigned core, Assembler program) {
        program.align();
        require(program.code.size() + bytes <= 128 * bytes, "SIMD program region overflow");
        entry_in[core] = core * 128 * bytes;
        for (unsigned offset = 0; offset < program.code.size(); offset += bytes) {
            Word word = 0;
            for (unsigned byte = 0; byte < bytes; ++byte) {
                word.bits(byte * 8 + 7, byte * 8) = program.code[offset + byte];
            }
            access(uint32_t(entry_in[core]) + offset, true, word);
        }
        access(uint32_t(entry_in[core]) + program.code.size(), true, Word(0));
    }

    void execute(bool stalls, bool fault = false) {
        host_mode_in = false;
        run_in = true;
        unsigned cycles;
        for (cycles = 0; cycles < 10000; ++cycles) {
            for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
                memory_enable_in[bank] = !stalls || (cycles + bank) % 7 >= 3;
            }
            tick();
            settle();
            bool all = true;
            for (unsigned core = 0; core < EC_CORES; ++core) {
                if (!fault) {
                    require(!OUT(fault_out[core]), "SIMD unexpected core fault");
                }
                all &= fault ? bool(OUT(fault_out[core])) : bool(OUT(halted_out[core]));
            }
            if (all) {
                break;
            }
        }
        require(cycles < 10000, "SIMD timeout");
        run_in = false;
        host_mode_in = true;
        for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
            memory_enable_in[bank] = true;
        }
        tick();
    }

    void fault(const std::vector<uint8_t>& code) {
        reset();
        Assembler p(EC_BITS);
        p.code = code;
        for (unsigned core = 0; core < EC_CORES; ++core) {
            program(core, p);
        }
        execute(true, true);
    }

#ifdef EC_SIMD
    // Independent scalar reference, using widened arithmetic and explicit sign fill.
    static uint32_t reference(unsigned op, uint32_t x, uint32_t y) {
        const unsigned shift = y % 32;
        switch (op) {
        case 0x80:
            return uint64_t(x) + y;
        case 0x81:
            return uint64_t(x) - y;
        case 0x82:
            return x & y;
        case 0x83:
            return x | y;
        case 0x84:
            return x ^ y;
        case 0x85:
            return uint64_t(x) << shift;
        case 0x86:
            return x >> shift;
        case 0x87:
            return x < y;
        case 0x88:
            return (x >> shift) | ((x & 0x80000000u) && shift ? ~uint32_t(0) << (32 - shift) : 0);
        case 0x89:
            return uint64_t(x) * y;
        case 0x8a:
            return (x ^ 0x80000000u) < (y ^ 0x80000000u);
        default:
            throw std::runtime_error("unexpected SIMD reference opcode");
        }
    }

    void arithmetic() {
        const uint32_t edges[] = {0xffffffff, 1, 0x80000000, 0x7fffffff, 0, 0x12345678, 32, 33};
        const uint32_t counts[] = {1, 31, 32, 33, 0xffffffff, 0, 0x80000000, 16};
        std::mt19937 random(0x512128);
        for (unsigned op = 0x80; op <= 0x8b; ++op) {
            for (unsigned alias = 0; alias < 4; ++alias) {
                for (unsigned round = 0; round < 6; ++round) {
                    reset();
                    std::vector<Word> expected(EC_CORES), scalar(EC_CORES);
                    for (unsigned core = 0; core < EC_CORES; ++core) {
                        Word left = 0, right = 0;
                        for (unsigned lane = 0; lane < EC_BITS / 32; ++lane) {
                            unsigned index = (lane + round * 2 + core) % 8;
                            uint32_t x = round < 4 ? edges[index] : uint32_t(random());
                            uint32_t y = round < 4 ? counts[index] : uint32_t(random());
                            if (alias == 3) {
                                y = x;
                            }
                            left.bits(lane * 32 + 31, lane * 32) = x;
                            right.bits(lane * 32 + 31, lane * 32) = y;
                            if (op != 0x8b) {
                                expected[core].bits(lane * 32 + 31, lane * 32) =
                                    reference(op, x, y);
                            }
                        }
                        if (op == 0x8b) {
                            for (unsigned lane = 0; lane < EC_BITS / 32; ++lane) {
                                expected[core].bits(lane * 32 + 31, lane * 32) = uint32_t(left);
                            }
                        }
                        unsigned destination = alias == 1 || alias == 3 ? 2 : alias == 2 ? 3 : 4;
                        Word finalLeft = destination == 2 ? expected[core] : left;
                        Word finalRight = destination == 3 ? expected[core] : right;
                        scalar[core] = Word(uint32_t(uint32_t(finalLeft) + uint32_t(finalRight)));
                        unsigned address = (dataBase + core * 5) * bytes;
                        access(address, true, left);
                        access(address + bytes, true, right);
                        access(address + 2 * bytes, true, Word(0));
                        access(address + 3 * bytes, true, Word(0));
                        access(address + 4 * bytes, true, pattern(core + 123));
                        Assembler p(EC_BITS);
                        p.li(6, address);
                        p.emit(Opcode::Load, 2, 6);
                        p.li(6, address + bytes);
                        p.emit(Opcode::Load, 3, 6);
                        p.emit(Opcode(op), destination, 2, alias == 3 ? 2 : 3);
                        p.li(6, address + 2 * bytes);
                        p.emit(Opcode::Store, destination, 6);
                        p.emit(Opcode::Add, 4, 2, 3);
                        p.li(6, address + 3 * bytes);
                        p.emit(Opcode::Store, 4, 6);
                        p.emit(Opcode::Halt);
                        program(core, p);
                    }
                    execute(round % 2);
                    for (unsigned core = 0; core < EC_CORES; ++core) {
                        unsigned address = (dataBase + core * 5 + 2) * bytes;
                        require(access(address, false) == expected[core],
                                "SIMD lane mismatch op=" + std::to_string(op) + " core=" +
                                    std::to_string(core) + " alias=" + std::to_string(alias) +
                                    " round=" + std::to_string(round));
                        require(access(address + bytes, false) == scalar[core],
                                "scalar zero extension after SIMD");
                        require(access(address + 2 * bytes, false) == pattern(core + 123),
                                "SIMD store guard");
                    }
                }
            }
        }
    }
#endif
};

int main() {
    SimdTest test;
#ifdef EC_SIMD
    test.arithmetic();
    for (unsigned op = 0x80; op <= 0x8b; ++op) {
        test.fault({uint8_t(op), EC_REGS, 0, 0});
        test.fault({uint8_t(op), 0, EC_REGS, 0});
        if (op != 0x8b) {
            test.fault({uint8_t(op), 0, 0, EC_REGS});
        }
    }
    std::vector<uint8_t> truncated(EC_BITS / 8, 0);
    truncated.back() = 0x80;
    test.fault(truncated);
#else
    for (unsigned op = 0x80; op <= 0x8b; ++op) {
        require(length(op) == 0, "SIMD encoding enabled in disabled build");
        test.fault({uint8_t(op), 0, 0, 0});
    }
#endif
    test.fault({0x8c, 0, 0, 0});
    test.reset();
    for (unsigned core = 0; core < EC_CORES; ++core) {
        Assembler p(EC_BITS);
        p.li(2, 42);
        p.emit(Opcode::Halt);
        test.program(core, p);
    }
    test.execute(false);
    std::cout << "SIMD regression PASS BUS_WIDTH=" << EC_BITS << " cores=" << EC_CORES
#ifdef EC_SIMD
              << " enabled: lanes, aliasing, stalls, scalar compatibility, faults, reset\n";
#else
              << " disabled: reserved opcodes fault, reset\n";
#endif
}
#endif
