#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "SystemHarness.h"
#include "../arch/instruction.h"
using namespace extreme;

struct SystemTest : Harness {
    unsigned cycles = 0;

    void reset() {
        run_in = false;
        host_mode_in = true;
        host_request_in = false;
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            memory_enable_in[i] = true;
        }
        tick(true);
        tick(true);
    }

    Word access(uint32_t addr, bool wr, Word value = 0) {
        host_address_in = addr;
        host_write_in = wr;
        host_data_in = value;
        host_request_in = true;
        settle();
        require(OUT(host_ready_out), "host not ready");
        tick();
        host_request_in = false;
        settle();
        require(OUT(host_response_out), "missing host response");
        Word r = read_word(OUT(host_result_out));
        tick();
        return r;
    }

    void program(unsigned core, Assembler a) {
        a.align();
        entry_in[core] = core * 128 * (EC_BITS / 8);
        for (unsigned off = 0; off < a.code.size(); off += EC_BITS / 8) {
            Word w = 0;
            for (unsigned j = 0; j < EC_BITS / 8; ++j) {
                w.bits(j * 8 + 7, j * 8) = a.code[off + j];
            }
            access((uint32_t)entry_in[core] + off, true, w);
        }
        // Defined speculative next word, including programs ending on a boundary.
        access((uint32_t)entry_in[core] + a.code.size(), true, Word(0));
    }

    void execute(bool stalls) {
        host_mode_in = false;
        run_in = true;
        for (cycles = 0; cycles < 100000; ++cycles) {
            for (unsigned b = 0; b < EC_BANKS; ++b) {
                memory_enable_in[b] = !stalls || ((cycles + 3 * b) % 11) > 2;
            }
            tick();
            settle();
            bool all = true;
            for (unsigned i = 0; i < EC_CORES; ++i) {
                require(!OUT(fault_out[i]), "CPU fault_out core=" + std::to_string(i) +
                                                " cycle=" + std::to_string(cycles));
                all &= bool(OUT(halted_out[i]));
            }
            if (all) {
                break;
            }
        }
        require(cycles < 100000, "CPU timeout");
        run_in = false;
        host_mode_in = true;
        for (unsigned b = 0; b < EC_BANKS; ++b) {
            memory_enable_in[b] = true;
        }
        tick();
    }

    void copies(unsigned count, bool stalls, bool loop, bool multi) {
        reset();
        constexpr unsigned bytes = EC_BITS / 8;
        for (unsigned c = 0; c < EC_CORES; ++c) {
            unsigned src = (256 + c * 768) * bytes;
            unsigned dest = ((EC_BANKS > 1 ? EC_BANK_WORDS : 2048) + c * 768) * bytes;
            Assembler p(EC_BITS);
            if (c == 0 || multi) {
                for (unsigned i = 0; i < count; ++i) {
                    access(src + i * bytes, true, pattern(i + 1 + c * 177));
                    access(dest + i * bytes, true, Word(0));
                }
                access(dest + count * bytes, true, pattern(0xbeef));
                p.li(0, src);
                p.li(1, dest);
                p.li(2, count);
                if (loop && count) {
                    p.emit(Opcode::Loop);
                    p.emit(Opcode::Load, 3, 0);
                    p.emit(Opcode::Move, 4, 3);
                    p.emit(Opcode::Store, 4, 1);
                    p.emit(Opcode::NextSource);
                    p.emit(Opcode::NextDestination);
                    p.emit(Opcode::End);
                } else {
                    p.emit(Opcode::Copy);
                }
            }
            p.emit(Opcode::Halt);
            program(c, p);
        }
        execute(stalls);
        // Count every clock from starting the core until HALT has drained stores.
        // Independent source/destination banks can read AND write a word per clock.
        if (count >= 1024 && !stalls && !loop && !multi && EC_DEPTH >= 8 && EC_BANKS >= 2) {
            const unsigned copied_bytes = count * bytes;
            const unsigned minimum_cycles = copied_bytes / (EC_BITS / 8);
            const unsigned measured_cycles = cycles + 1;
            const unsigned maximum_cycles = minimum_cycles + 64; // bounded setup + drain
            require(measured_cycles >= minimum_cycles && measured_cycles <= maximum_cycles,
                    "memcpy bandwidth: actual=" + std::to_string(measured_cycles) +
                        " minimum=" + std::to_string(minimum_cycles) +
                        " maximum=" + std::to_string(maximum_cycles));
            std::cout << "memcpy bandwidth PASS: " << measured_cycles << " clocks, minimum "
                      << minimum_cycles << ", maximum " << maximum_cycles << '\n';
        }
        for (unsigned reg = 0; reg < 3; ++reg) {
            debug_register_index_in = reg;
            settle();
            for (unsigned c = 0; c < (multi ? EC_CORES : 1); ++c) {
                unsigned base =
                    reg == 0 ? (256 + c * 768) * (EC_BITS / 8)
                             : ((EC_BANKS > 1 ? EC_BANK_WORDS : 2048) + c * 768) * (EC_BITS / 8);
                Word expected = reg == 2 ? Word(0) : Word(base + count * (EC_BITS / 8));
                require(read_word(OUT(debug_register_out[c])) == expected,
                        "copy register postcondition");
            }
        }
        for (unsigned c = 0; c < (multi ? EC_CORES : 1); ++c) {
            unsigned dest = ((EC_BANKS > 1 ? EC_BANK_WORDS : 2048) + c * 768) * bytes;
            for (unsigned i = 0; i < count; ++i) {
                require(access(dest + i * bytes, false) == pattern(i + 1 + c * 177),
                        "copy mismatch word=" + std::to_string(i));
            }
            require(access(dest + count * bytes, false) == pattern(0xbeef),
                    "copy overran destination");
        }
        std::cout << "copy words=" << count << " loop=" << loop << " multicore=" << multi
                  << " stalls=" << stalls << " cycles=" << cycles + 1 << " payload bytes/cycle="
                  << double(count * bytes * (multi ? EC_CORES : 1)) / (cycles + 1) << '\n';
    }

    void scalar_memory_and_calls() {
        reset();
        const unsigned bytes = EC_BITS / 8;
        for (unsigned c = 0; c < EC_CORES; ++c) {
            unsigned address = (3000 + c * 8) * bytes;
            Word initial = 0;
            for (unsigned i = 0; i < bytes; ++i) {
                initial.bits(i * 8 + 7, i * 8) = 0xaa;
            }
            access(address, true, initial);
            Assembler p(EC_BITS);
            p.li(0, address);
            p.li(3, 0xf1234567);
            p.emit(Opcode::StoreScalar, 3, 0, 2);
            p.li(0, address + bytes - 4);
            p.li(3, 0x8001);
            p.emit(Opcode::StoreScalar, 3, 0, 1);
            p.li(0, address + bytes - 1);
            p.li(3, 0x80);
            p.emit(Opcode::StoreScalar, 3, 0, 0);
            p.emit(Opcode::Barrier);
            p.emit(Opcode::LoadScalar, 4, 0, 0); // signed byte
            p.li(1, address + bytes);
            p.emit(Opcode::Store, 4, 1);
            p.li(0, address + bytes - 4);
            p.emit(Opcode::LoadScalar, 4, 0, 1); // signed half
            p.li(1, address + 2 * bytes);
            p.emit(Opcode::Store, 4, 1);
            p.emit(Opcode::LoadScalar, 7, 0, 5); // unsigned half
            p.li(0, address);
            p.emit(Opcode::LoadScalar, 6, 0, 2);
            p.emit(Opcode::Halt);
            program(c, p);
        }
        execute(true);
        for (unsigned c = 0; c < EC_CORES; ++c) {
            unsigned address = (3000 + c * 8) * bytes;
            Word expected = 0;
            for (unsigned i = 0; i < bytes; ++i) {
                expected.bits(i * 8 + 7, i * 8) = 0xaa;
            }
            expected.bits(31, 0) = 0xf1234567;
            expected.bits(EC_BITS - 17, EC_BITS - 32) = 0x8001;
            expected.bits(EC_BITS - 1, EC_BITS - 8) = 0x80;
            require(access(address, false) == expected, "scalar store byte-enable preservation");
            require(access(address + bytes, false) == Word(0xffffff80u), "LB sign extension");
            require(access(address + 2 * bytes, false) == Word(0xffff8001u), "LH sign extension");
        }
        debug_register_index_in = 6;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == Word(0xf1234567u), "LW result");
        }
        debug_register_index_in = 7;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == Word(0x8001), "LHU result");
        }
        reset();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            unsigned base = c * 128 * bytes;
            access((3000 + c * 8) * bytes, true, pattern(123));
            Assembler p(EC_BITS);
            p.emit(Opcode::StackLimit, 0, 0, 0, 2);
            p.li(6, (3000 + c * 8) * bytes);
            p.emit(Opcode::Load, 0, 6);
            p.emit(Opcode::Move, 1, 0);
            p.align();
            unsigned callA = p.code.size();
            p.emit(Opcode::Call);
            p.align();
            p.emit(Opcode::Halt);
            p.align();
            unsigned fnA = p.code.size();
            unsigned callB = p.code.size();
            p.emit(Opcode::Call);
            p.align();
            p.emit(Opcode::Return);
            p.align();
            unsigned fnB = p.code.size();
            p.li(4, 41);
            p.li(5, 1);
            p.emit(Opcode::Add, 4, 4, 5);
            p.emit(Opcode::Return);
            for (unsigned j = 0; j < 4; ++j) {
                p.code[callA + 1 + j] = ((base + fnA) / bytes) >> (8 * j);
                p.code[callB + 1 + j] = ((base + fnB) / bytes) >> (8 * j);
            }
            program(c, p);
        }
        execute(true);
        debug_register_index_in = 1;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == pattern(123),
                    "call damaged non-stack register");
            require(OUT(debug_sp_out[c]) == 0, "return stack did not unwind");
        }
        debug_register_index_in = 0;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            for (unsigned j = 2; j < EC_BITS / 32; ++j) {
                require(uint32_t(read_word(OUT(debug_register_out[c])) >> (j * 32)) ==
                            uint32_t(pattern(123) >> (j * 32)),
                        "call overwrote another register lane");
            }
        }
        debug_register_index_in = 4;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == Word(42), "nested call result");
        }
    }

    void scalar_and_branches() {
        reset();
        std::vector<uint32_t> expected;
        for (unsigned c = 0; c < EC_CORES; ++c) {
            Assembler p(EC_BITS);
            std::mt19937 rng(212);
            unsigned record = 0;
            for (unsigned code = 0x20; code <= 0x27; ++code) {
                for (unsigned sample = 0; sample < 2; ++sample) {
                    uint32_t x = rng(), y = rng(), answer = 0;
                    if (sample == 0) {
                        x = 0xffffffffu;
                        y = 33;
                    } // wrapping and masked shifts
                    switch (code) {
                    case 0x20:
                        answer = x + y;
                        break;
                    case 0x21:
                        answer = x - y;
                        break;
                    case 0x22:
                        answer = x & y;
                        break;
                    case 0x23:
                        answer = x | y;
                        break;
                    case 0x24:
                        answer = x ^ y;
                        break;
                    case 0x25:
                        answer = x << (y & 31);
                        break;
                    case 0x26:
                        answer = x >> (y & 31);
                        break;
                    case 0x27:
                        answer = x < y;
                        break;
                    }
                    if (c == 0) {
                        expected.push_back(answer);
                    }
                    p.li(3, x);
                    p.li(4, y);
                    p.emit(Opcode(code), 5, 3, 4);
                    p.li(0, (3000 + c * 128 + record++) * (EC_BITS / 8));
                    p.emit(Opcode::Store, 5, 0);
                }
            }
            p.li(3, 0);
            p.align();
            unsigned bz = p.code.size();
            p.emit(Opcode::BranchZero, 3, 0, 0, 0);
            p.code.push_back(0xff);
            p.align();
            unsigned target = (uint32_t(c * 128 * (EC_BITS / 8)) + p.code.size()) / (EC_BITS / 8);
            for (unsigned j = 0; j < 4; ++j) {
                p.code[bz + 2 + j] = target >> (8 * j);
            }
            p.li(3, 1);
            p.emit(Opcode::BranchZero, 3, 0, 0, 0); // not taken
            p.align();
            unsigned jump = p.code.size();
            p.emit(Opcode::Jump);
            p.code.push_back(0xff);
            p.align();
            target = (uint32_t(c * 128 * (EC_BITS / 8)) + p.code.size()) / (EC_BITS / 8);
            for (unsigned j = 0; j < 4; ++j) {
                p.code[jump + 1 + j] = target >> (8 * j);
            }
            p.li(6, 0xabcde123);
            p.emit(Opcode::Move, 7, 6);
            p.emit(Opcode::Halt);
            require(p.code.size() < 128 * (EC_BITS / 8), "program exceeds allocated region");
            program(c, p);
        }
        execute(true);
        debug_register_index_in = 7;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == Word(0xabcde123),
                    "branch/MOV mismatch");
        }
        for (unsigned c = 0; c < EC_CORES; ++c) {
            for (unsigned i = 0; i < expected.size(); ++i) {
                require(access((3000 + c * 128 + i) * (EC_BITS / 8), false) == Word(expected[i]),
                        "scalar oracle mismatch");
            }
        }
    }

    void stackPointerAccess() {
        reset();
        for (unsigned core = 0; core < EC_CORES; ++core) {
            Assembler p(EC_BITS);
            p.li(0, 0x12345678);
            p.emit(Opcode::StackLimit, 0, 0, 0, 3);
            p.li(2, 3);
            p.emit(Opcode::WriteStackPointer, 2);
            p.emit(Opcode::ReadStackPointer, 3);
            p.li(2, 0);
            p.emit(Opcode::WriteStackPointer, 2);
            p.emit(Opcode::ReadStackPointer, 4);
            p.emit(Opcode::Halt);
            program(core, p);
        }
        execute(true);
        for (unsigned index : {0u, 3u, 4u}) {
            debug_register_index_in = index;
            settle();
            for (unsigned core = 0; core < EC_CORES; ++core) {
                require(read_word(OUT(debug_register_out[core])) == Word(index == 0   ? 0x12345678u
                                                                         : index == 3 ? 3u
                                                                                      : 0u),
                        "GETSP/SETSP changed register contents or returned the wrong count");
            }
        }
    }

    void faults() {
        for (unsigned kind = 0; kind < 27; ++kind) {
            reset();
            for (unsigned c = 0; c < EC_CORES; ++c) {
                Assembler p(EC_BITS);
                if (c == 0) {
                    if (kind == 0) {
                        p.code.push_back(0xff);
                    }
                    if (kind == 1) {
                        p.li(EC_REGS, 1);
                    }
                    if (kind == 2) {
                        p.li(0, 1);
                        p.emit(Opcode::Load, 3, 0);
                    }
                    if (kind == 3) {
                        p.li(0, EC_BANKS * EC_BANK_WORDS * (EC_BITS / 8));
                        p.emit(Opcode::Load, 3, 0);
                    }
                    if (kind == 4) {
                        p.emit(Opcode::End);
                    }
                    if (kind == 5) {
                        p.li(2, 0);
                        p.emit(Opcode::Loop);
                    }
                    if (kind == 6) {
                        p.code.assign(EC_BITS / 8 - 1, 0);
                        p.code.push_back(0x10);
                    }
                    if (kind == 7) {
                        p.li(2, 1);
                        p.emit(Opcode::Loop);
                        p.emit(Opcode::Loop);
                    }
                    if (kind == 8) {
                        p.li(0, 1);
                        p.emit(Opcode::Store, 3, 0);
                    }
                    if (kind == 9) {
                        p.emit(Opcode::Add, 3, 0, 255);
                    }
                    if (kind == 11) {
                        p.emit(Opcode::Return);
                    }
                    if (kind == 12) {
                        p.emit(Opcode::StackLimit, 0, 0, 0, 2);
                        p.align();
                        unsigned target = p.code.size() / (EC_BITS / 8);
                        p.emit(Opcode::Call, 0, 0, 0, target);
                    }
                    if (kind == 13) {
                        p.li(3, 3);
                        p.emit(Opcode::CallRegister, 3);
                    }
                    if (kind == 14) {
                        p.li(0, 1);
                        p.emit(Opcode::LoadScalar, 3, 0, 2);
                    }
                    if (kind == 16) {
                        p.emit(Opcode::TaskIssue, EC_REGS, 0, 0);
                    }
                    if (kind == 17) {
                        p.emit(Opcode::TaskIssue, 0, 0, EC_REGS);
                    }
                    if (kind == 18) {
                        p.emit(Opcode::TaskRead, 2, 3, 2);
                    }
                    if (kind == 19) {
                        p.emit(Opcode::TaskDisarm, 2, EC_REGS);
                    }
                    if (kind == 20) {
                        p.emit(Opcode::TaskId, EC_REGS);
                    }
                    if (kind == 21) {
                        p.emit(Opcode::TaskFinish, 0);
                    }
                    if (kind == 22) {
                        p.emit(Opcode::TaskAbort, 0);
                    }
                    if (kind == 23) {
                        p.emit(Opcode::ReadStackPointer, EC_REGS);
                    }
                    if (kind == 24) {
                        p.emit(Opcode::WriteStackPointer, EC_REGS);
                    }
                    if (kind == 25 || kind == 26) {
                        p.emit(Opcode::StackLimit, 0, 0, 0, 2);
                        p.li(2, kind == 25 ? 3 : 0xffffffffu);
                        p.emit(Opcode::WriteStackPointer, 2);
                    }
                    if (kind == 15) {
                        p.emit(Opcode::StackLimit, 0, 0, 0, EC_REGS * (EC_BITS / 32) + 1);
                    }
                }
                p.emit(Opcode::Halt);
                program(c, p);
            }
            if (kind == 10) {
                entry_in[0] = 1;
            }
            host_mode_in = false;
            run_in = true;
            bool failed = false;
            for (unsigned n = 0; n < 500; ++n) {
                tick();
                settle();
                if (OUT(fault_out[0])) {
                    failed = true;
                    break;
                }
            }
            require(failed, "missing CPU fault");
        }
        reset(); // cancel outstanding requests after a fault
    }

    void math() {
        reset();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            Assembler p(EC_BITS);
            p.li(3, 0xffffffffu);
            p.li(4, 2);
            p.emit(Opcode::Add, 5, 3, 4); // wrap to 1
            p.emit(Opcode::Sub, 6, 4, 5);
            p.emit(Opcode::ShiftLeft, 6, 6, 4); // 4
            p.emit(Opcode::Xor, 6, 6, 5);       // 5
            p.li(0, (3000 + c * 8) * (EC_BITS / 8));
            p.emit(Opcode::Store, 6, 0);
            p.emit(Opcode::Barrier);
            p.emit(Opcode::Load, 7, 0);
            p.emit(Opcode::Halt);
            program(c, p);
        }
        execute(true);
        debug_register_index_in = 7;
        settle();
        for (unsigned c = 0; c < EC_CORES; ++c) {
            require(read_word(OUT(debug_register_out[c])) == Word(5), "math/barrier mismatch");
        }
    }
};

int main() {
    try {
        SystemTest t;
        t.copies(0, false, false, false);
        t.copies(1, true, false, false);
        t.copies(257, false, false, false);
        t.copies(1024, false, false, false);
        if (EC_BANKS >= 2) {
            t.copies(2048, false, false, false);
        }
        t.copies(97, true, false, true);
        t.copies(19, true, true, true);
        t.math();
        t.scalar_memory_and_calls();
        t.scalar_and_branches();
        t.stackPointerAccess();
        t.faults();
        std::cout << "System PASS BUS_WIDTH=" << EC_BITS << " depth=" << EC_DEPTH << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
