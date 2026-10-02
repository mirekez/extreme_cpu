#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "SystemHarness.h"
#include "../arch/instruction.h"
#include <array>
using namespace extreme;

struct TasksTest : Harness {
    static constexpr unsigned bytes = EC_BITS / 8, output = 3600 * bytes, data = 3500 * bytes;

    static unsigned entry(unsigned t) {
        return (256 + t * 128) * bytes;
    }

    Word access(unsigned addr, bool write, Word value = 0) {
        host_address_in = addr;
        host_write_in = write;
        host_data_in = value;
        host_request_in = true;
        settle();
        require(OUT(host_ready_out), "host request");
        tick();
        host_request_in = false;
        settle();
        require(OUT(host_response_out), "host response");
        Word result = read_word(OUT(host_result_out));
        tick();
        return result;
    }

    void load(unsigned addr, Assembler p) {
        p.align();
        p.code.resize(p.code.size() + bytes, 0);
        for (unsigned off = 0; off < p.code.size(); off += bytes) {
            Word w = 0;
            for (unsigned j = 0; j < bytes; ++j) {
                w.bits(j * 8 + 7, j * 8) = p.code[off + j];
            }
            access(addr + off, true, w);
        }
    }

    void expectZero(Assembler& p, unsigned base, unsigned reg) {
        p.align();
        unsigned offset = p.code.size();
        p.emit(Opcode::BranchZero, reg, 0, 0, 0);
        p.li(6, 0xbad);
        p.emit(Opcode::TaskAbort, 6);
        p.align();
        unsigned word = (base + p.code.size()) / bytes;
        for (unsigned j = 0; j < 4; ++j) {
            p.code[offset + 2 + j] = word >> (8 * j);
        }
    }

    void issue(Assembler& p, unsigned base, unsigned t, uint32_t deps) {
        p.li(2, t);
        p.li(3, entry(t));
        p.li(4, deps);
        p.emit(Opcode::TaskIssue, 2, 3, 4);
        expectZero(p, base, 2);
    }

    void read(Assembler& p, unsigned base, unsigned t) {
        p.li(3, t);
        p.emit(Opcode::TaskRead, 2, 3, 4);
        expectZero(p, base, 4);
    }

    void run(unsigned mode, bool stalls) {
        // 0 fan-out/join; 1 hardware abort propagation; 2 consumer-inspected
        // error; 3 a dispatched task builds another graph; 4 dependency chain.
        run_in = false;
        host_mode_in = true;
        host_request_in = false;
        for (unsigned b = 0; b < EC_BANKS; ++b) {
            memory_enable_in[b] = true;
        }
        tick(true);
        tick(true);
        access(output, true, Word(0x12345678));
        for (unsigned t = 0; t < 10; ++t) {
            access(data + t * bytes, true, Word(0));
        }
        Assembler main(EC_BITS), idle(EC_BITS);
        idle.emit(Opcode::Halt);
        load(200 * bytes, idle);
        main.emit(Opcode::TaskId, 2);
        main.li(3, 0xffffffff);
        main.emit(Opcode::Xor, 2, 2, 3);
        expectZero(main, 0, 2);
        main.li(3, 31);
        main.emit(Opcode::TaskRead, 2, 3, 4);
        main.li(2, 3);
        main.emit(Opcode::Sub, 4, 4, 2);
        expectZero(main, 0, 4);
        main.emit(Opcode::TaskStatus, 2, 3);
        expectZero(main, 0, 2);
        issue(main, 0, 10, 1u << 31);
        main.li(3, 10);
        main.emit(Opcode::TaskDisarm, 2, 3);
        expectZero(main, 0, 2);
        for (unsigned t = 0; t < 8; ++t) {
            main.li(5, 0x100 + t);
            main.li(6, data + t * bytes);
            main.emit(Opcode::StoreScalar, 5, 6, 2);
        }
        if (mode == 3) {
            issue(main, 0, 0, 0);
        } else {
            issue(main, 0, 9, 1u << 8); // continuation of reduction
            issue(main, 0, 8, mode == 4 ? 1u << 7 : 255);
            for (int t = 7; t >= 0; --t) {
                issue(main, 0, t, mode == 4 && t > 0 ? 1u << (t - 1) : 0);
            }
        }
        main.emit(Opcode::Halt);
        require(main.code.size() < 200 * bytes, "main code overlap");
        load(0, main);
        for (unsigned t = 0; t < 8; ++t) {
            Assembler p(EC_BITS);
            unsigned base = entry(t);
            if (mode == 3 && t == 0) {
                issue(p, base, 9, 1u << 8);
                issue(p, base, 8, 255);
                for (unsigned j = 1; j < 8; ++j) {
                    issue(p, base, j, 1); // children wait for parent's result
                }
            }
            p.emit(Opcode::TaskId, 5);
            p.li(6, t);
            p.emit(Opcode::Xor, 6, 5, 6);
            expectZero(p, base, 6);
            p.li(6, data + t * bytes);
            p.emit(Opcode::LoadScalar, 5, 6, 2);
            p.li(6, 0x100 + t);
            p.emit(Opcode::Xor, 6, 5, 6);
            expectZero(p, base, 6);
            // Delay to exercise concurrent executions and queuing beyond core count.
            p.li(2, 20);
            p.emit(Opcode::Loop);
            p.emit(Opcode::Nop);
            p.emit(Opcode::End);
            p.li(5, t + 1);
            if (mode == 4 && t > 0) {
                read(p, base, t - 1);
                p.emit(Opcode::Add, 5, 5, 2);
            }
            if (mode == 3 && t > 0) {
                read(p, base, 0);
                p.emit(Opcode::Add, 5, 5, 2);
            }
            if ((mode == 1 || mode == 2) && t == 3) {
                p.li(5, 0x80000003);
            }
            p.li(6, data + t * bytes);
            p.emit(Opcode::StoreScalar, 5, 6, 2);
            p.emit(mode == 1 && t == 3 ? Opcode::TaskAbort : Opcode::TaskFinish, 5);
            require(p.code.size() < 128 * bytes, "worker code overlap");
            load(base, p);
        }
        Assembler reduce(EC_BITS);
        unsigned base = entry(8);
        reduce.li(5, 0);
        for (unsigned t = (mode == 4 ? 7 : 0); t < 8; ++t) {
            read(reduce, base, t);
            if (mode == 2) {
                reduce.li(6, 31);
                reduce.emit(Opcode::ShiftRight, 6, 2, 6);
                reduce.align();
                unsigned off = reduce.code.size();
                reduce.emit(Opcode::BranchZero, 6, 0, 0, 0);
                reduce.emit(Opcode::TaskAbort, 2);
                reduce.align();
                unsigned target = (base + reduce.code.size()) / bytes;
                for (unsigned j = 0; j < 4; ++j) {
                    reduce.code[off + 2 + j] = target >> (8 * j);
                }
            }
            reduce.emit(Opcode::Add, 5, 5, 2);
            // Finishing publishes stores too: dependent reads must see each leaf's memory result.
            reduce.li(6, data + t * bytes);
            reduce.emit(Opcode::LoadScalar, 6, 6, 2);
            reduce.emit(Opcode::Xor, 6, 6, 2);
            expectZero(reduce, base, 6);
        }
        reduce.li(6, output);
        reduce.emit(Opcode::StoreScalar, 5, 6, 2);
        reduce.emit(Opcode::TaskFinish, 5);
        require(reduce.code.size() < 128 * bytes, "reduce code overlap");
        load(base, reduce);
        Assembler last(EC_BITS);
        read(last, entry(9), 8);
        last.emit(Opcode::TaskFinish, 2);
        load(entry(9), last);
        for (unsigned c = 0; c < EC_CORES; ++c) {
            entry_in[c] = (c == 0) ? 0 : 200 * bytes;
        }
        host_mode_in = false;
        run_in = true;
        std::array<unsigned, 32> launched{};
        unsigned maxRunning = 0, cycles = 0;
        for (; cycles < 100000; ++cycles) {
            for (unsigned b = 0; b < EC_BANKS; ++b) {
                memory_enable_in[b] = !stalls || (cycles + 3 * b) % 13 > 3;
            }
            tick();
            settle();
            bool halted = true;
            for (unsigned c = 0; c < EC_CORES; ++c) {
                require(!OUT(fault_out[c]), "task system core fault mode=" + std::to_string(mode));
                halted &= bool(OUT(halted_out[c]));
                if (OUT(task_launch_out[c])) {
                    unsigned t = OUT(task_launch_id_out[c]);
                    require(t < 10 && ++launched[t] == 1, "duplicate/invalid task dispatch");
                }
            }
            unsigned running = 0;
            for (unsigned t = 0; t < 10; ++t) {
                task_debug_id_in = t;
                settle();
                if (OUT(task_debug_state_out) == 3) {
                    ++running;
                }
            }
            maxRunning = std::max(maxRunning, running);
            if (halted && OUT(tasks_idle_out)) {
                break;
            }
        }
        require(cycles < 100000, "task system timeout");
        if (EC_CORES > 1 && mode != 4) {
            require(maxRunning > 1, "parallel map tasks never overlapped");
        }
        uint32_t total = mode == 3 ? 43 : 36;
        for (unsigned t = 0; t < 11; ++t) {
            task_debug_id_in = t;
            settle();
            unsigned state = OUT(task_debug_state_out);
            uint32_t result = OUT(task_debug_result_out);
            if (t == 10) {
                require(state == 6 && result == 0xfffffffd, "disarmed task state/result");
            } else if (mode == 1 && t >= 8) {
                require(state == 6 && result == 0x80000003 && !launched[t],
                        "abort continuation cancellation");
            } else if (mode == 2 && t == 9) {
                require(state == 6 && result == 0x80000003 && !launched[t],
                        "consumer abort cancellation");
            } else if ((mode == 1 && t == 3) || (mode == 2 && t == 8)) {
                require(state == 5 && result == 0x80000003, "failed task result");
            } else {
                uint32_t expected = t >= 8               ? total
                                    : mode == 4          ? (t + 1) * (t + 2) / 2
                                    : mode == 3 && t > 0 ? t + 2
                                                         : t + 1;
                if (mode == 2 && t == 3) {
                    expected = 0x80000003;
                }
                require(state == 4 && result == expected && launched[t] == 1,
                        "task result mismatch mode=" + std::to_string(mode) +
                            " task=" + std::to_string(t));
            }
        }
        run_in = false;
        host_mode_in = true;
        for (unsigned b = 0; b < EC_BANKS; ++b) {
            memory_enable_in[b] = true;
        }
        tick();
        require(uint32_t(access(output, false)) == ((mode == 1 || mode == 2) ? 0x12345678 : total),
                "reduction memory result/abort side effect");
        std::cout << "Tasks system PASS mode=" << mode << " stalls=" << stalls
                  << " clocks=" << cycles + 1 << " parallel=" << maxRunning << '\n';
    }
};

int main() {
    try {
        TasksTest t;
        for (unsigned mode = 0; mode < 5; ++mode) {
            for (bool stalls : {false, true}) {
                t.run(mode, stalls);
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
