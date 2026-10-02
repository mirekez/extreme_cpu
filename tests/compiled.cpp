#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "SystemHarness.h"
#include <fstream>

struct CompiledTest : Harness {
    Word access(uint32_t address, bool write, Word data = 0) {
        host_address_in = address;
        host_write_in = write;
        host_data_in = data;
        host_request_in = true;
        settle();
        require(OUT(host_ready_out), "host request rejected");
        tick();
        host_request_in = false;
        settle();
        require(OUT(host_response_out), "missing host response");
        Word result = read_word(OUT(host_result_out));
        tick();
        return result;
    }

    unsigned runImage(const std::string& path, uint32_t expected, unsigned limit, bool copyCheck,
                      bool stalls) {
        std::ifstream file(path, std::ios::binary);
        require(bool(file), "cannot open image");
        auto field = [&]() {
            uint32_t v = 0;
            for (unsigned i = 0; i < 4; ++i) {
                int c = file.get();
                require(c != EOF, "truncated image header");
                v |= uint32_t(uint8_t(c)) << (i * 8);
            }
            return v;
        };
        require(field() == 0x58434345 && field() == 1, "image format mismatch");
        require(field() == EC_BITS && field() == EC_REGS && field() == EC_BANKS &&
                    field() == EC_BANK_WORDS,
                "image/RTL configuration mismatch");
        unsigned entry = field(), idle = field(), result = field(), size = field();
        require(size == EC_BANK_WORDS * EC_BANKS * (EC_BITS / 8), "image memory size");
        run_in = false;
        host_mode_in = true;
        for (unsigned b = 0; b < EC_BANKS; ++b) {
            memory_enable_in[b] = true;
        }
        tick(true);
        tick(true);
        for (unsigned i = 0; i < size; i += EC_BITS / 8) {
            Word word = 0;
            for (unsigned j = 0; j < EC_BITS / 8; ++j) {
                int c = file.get();
                require(c != EOF, "truncated image payload");
                word.bits(j * 8 + 7, j * 8) = uint8_t(c);
            }
            access(i, true, word);
        }
        require(file.get() == EOF, "trailing image data");
        const unsigned source = 2048 * (EC_BITS / 8), dest = (EC_BANK_WORDS + 1024) * (EC_BITS / 8);
        if (copyCheck) {
            require(EC_BANKS >= 2, "copy benchmark needs two banks");
            for (unsigned i = 0; i < 1024; ++i) {
                access(source + i * (EC_BITS / 8), true, pattern(i + 77));
                access(dest + i * (EC_BITS / 8), true, Word(0));
            }
        }
        for (unsigned c = 0; c < EC_CORES; ++c) {
            entry_in[c] = (c == 0) ? entry : idle;
        }
        host_mode_in = false;
        run_in = true;
        unsigned cycles = 0;
        for (; cycles < limit;) {
            for (unsigned b = 0; b < EC_BANKS; ++b) {
                memory_enable_in[b] = !stalls || ((cycles + b) % 11) > 2;
            }
            tick();
            ++cycles;
            settle();
            bool done = OUT(tasks_idle_out);
            for (unsigned c = 0; c < EC_CORES; ++c) {
                require(!OUT(fault_out[c]),
                        "compiled program CPU fault at cycle " + std::to_string(cycles));
                done &= bool(OUT(halted_out[c]));
            }
            if (done) {
                break;
            }
        }
        require(cycles < limit, "compiled program timeout/cycle bound");
        run_in = false;
        host_mode_in = true;
        for (unsigned b = 0; b < EC_BANKS; ++b) {
            memory_enable_in[b] = true;
        }
        tick();
        Word word = access(result & ~(EC_BITS / 8 - 1), false);
        uint32_t got = (uint32_t)(word >> ((result % (EC_BITS / 8)) * 8));
        require(got == expected, "compiled result got=" + std::to_string(got) +
                                     " expected=" + std::to_string(expected));
        if (copyCheck) {
            for (unsigned i = 0; i < 1024; ++i) {
                require(access(dest + i * (EC_BITS / 8), false) == pattern(i + 77),
                        "compiled memcpy mismatch");
            }
        }
        std::cout << "compiled PASS " << path << " result=" << got << " clocks=" << cycles << '\n';
        return cycles;
    }
};

int main(int argc, char** argv) {
    try {
        require(argc >= 3, "usage: compiled image.ecx expected [cycle_limit] [copy|stalls]");
        CompiledTest t;
        t.runImage(argv[1], std::stoul(argv[2]), argc > 3 ? std::stoul(argv[3]) : 5000000,
                   argc > 4 && std::string(argv[4]) == "copy",
                   argc > 4 && std::string(argv[4]) == "stalls");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
#endif
