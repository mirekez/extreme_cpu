#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "../tests/SystemHarness.h"
#include "sim/Media.h"
#include <fstream>
#include <poll.h>

struct Board : Harness {
    std::string console;
    unsigned launches = 0, rx_frames = 0, tx_frames = 0;

    Word access(uint32_t address, bool write, Word data = 0) {
        host_address_in = address;
        host_write_in = write;
        host_data_in = data;
        host_request_in = true;
        settle();
        require(OUT(host_ready_out), "host memory not ready");
        tick();
        host_request_in = false;
        settle();
        require(OUT(host_response_out), "host memory response missing");
        Word result = read_word(OUT(host_result_out));
        tick();
        return result;
    }

    void run(const std::string& path, const std::string& socket, uint64_t limit, bool stalls,
             bool require_task, uint64_t progress, bool allow_idle_network) {
        std::ifstream file(path, std::ios::binary);
        require(bool(file), "cannot open image");
        auto field = [&]() {
            uint32_t value = 0;
            for (unsigned byte = 0; byte < 4; ++byte) {
                int c = file.get();
                require(c != EOF, "truncated image header");
                value |= uint32_t(uint8_t(c)) << (byte * 8);
            }
            return value;
        };
        require(field() == 0x58434345 && field() == 1, "image format");
        require(field() == EC_BITS && field() == EC_REGS && field() == EC_RAM_BANKS &&
                    field() == EC_BANK_WORDS,
                "image does not match board RAM configuration");
        unsigned entry = field(), idle = field(), result_address = field(), size = field();
        require(size == EC_RAM_BANKS * EC_BANK_WORDS * (EC_BITS / 8), "image size");
        host_mode_in = true;
        for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
            memory_enable_in[bank] = true;
        }
        tick(true);
        tick(true);
        for (unsigned offset = 0; offset < size; offset += EC_BITS / 8) {
            Word word = 0;
            for (unsigned byte = 0; byte < EC_BITS / 8; ++byte) {
                int c = file.get();
                require(c != EOF, "truncated image payload");
                word.bits(byte * 8 + 7, byte * 8) = uint8_t(c);
            }
            access(offset, true, word);
        }
        require(file.get() == EOF, "trailing image payload");
        for (unsigned core = 0; core < EC_CORES; ++core) {
            entry_in[core] = core == 0 ? entry : idle;
        }
        Media media(socket);
        std::vector<uint8_t> rx, tx;
        unsigned rx_offset = 0;
        std::deque<uint8_t> keyboard;
        host_mode_in = false;
        run_in = true;
        uint64_t cycle;
        for (cycle = 0; limit == 0 || cycle < limit; ++cycle) {
            if (progress && cycle && cycle % progress == 0) {
                std::cerr << "Extreme progress cycles=" << cycle
                          << " pc=" << uint32_t(OUT(debug_pc_out[0]))
                          << " sp=" << uint32_t(OUT(debug_sp_out[0])) << '\n';
            }
            if (cycle % 128 == 0) {
                media.poll();
                pollfd input{STDIN_FILENO, POLLIN, 0};
                if (keyboard.size() < 256 && ::poll(&input, 1, 0) > 0 && (input.revents & POLLIN)) {
                    uint8_t buffer[128];
                    auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
                    if (count > 0) {
                        keyboard.insert(keyboard.end(), buffer, buffer + count);
                    }
                }
            }
            uart_rx_valid_in = !keyboard.empty();
            if (!keyboard.empty()) {
                uart_rx_data_in = keyboard.front();
            }
            if (rx.empty() && media.available()) {
                rx = media.take();
                rx_offset = 0;
            }
            ethernet_rx_valid_in = !rx.empty();
            if (!rx.empty()) {
                unsigned count = std::min(unsigned(EC_BITS / 8), unsigned(rx.size() - rx_offset));
                ethernet_rx_count_in = count;
                ethernet_rx_last_in = rx_offset + count == rx.size();
                ethernet_rx_data_in = 0;
                for (unsigned byte = 0; byte < count; ++byte) {
                    ethernet_rx_data_in.bits(byte * 8 + 7, byte * 8) = rx[rx_offset + byte];
                }
            }
            uart_ready_in = !stalls || cycle % 7 != 0;
            ethernet_tx_ready_in = media.ready() && (!stalls || cycle % 7 > 2);
            for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
                memory_enable_in[bank] = !stalls || (cycle + bank) % 11 > 2;
            }
            settle();
            if (uart_rx_valid_in && OUT(uart_rx_ready_out)) {
                keyboard.pop_front();
            }
            if (OUT(uart_valid_out) && uart_ready_in) {
                char c = uint8_t(OUT(uart_data_out));
                console.push_back(c);
                std::cout.put(c);
                std::cout.flush();
            }
            if (ethernet_rx_valid_in && OUT(ethernet_rx_ready_out)) {
                rx_offset += uint32_t(ethernet_rx_count_in);
                if (ethernet_rx_last_in) {
                    rx.clear();
                    ++rx_frames;
                }
            }
            if (OUT(ethernet_tx_valid_out) && ethernet_tx_ready_in) {
                unsigned count = uint32_t(OUT(ethernet_tx_count_out));
                require(count > 0 && count <= EC_BITS / 8, "invalid Ethernet beat count");
                Word word = read_word(OUT(ethernet_tx_data_out));
                for (unsigned byte = 0; byte < count; ++byte) {
                    tx.push_back(uint8_t(uint32_t(word >> (byte * 8))));
                }
                if (OUT(ethernet_tx_last_out)) {
                    media.send_frame(tx);
                    tx.clear();
                    ++tx_frames;
                }
            }
            for (unsigned core = 0; core < EC_CORES; ++core) {
                if (OUT(task_launch_out[core])) {
                    ++launches;
                }
            }
            tick();
            settle();
            bool done = OUT(tasks_idle_out);
            for (unsigned core = 0; core < EC_CORES; ++core) {
                if (OUT(fault_out[core])) {
                    throw std::runtime_error(
                        "CPU fault on core " + std::to_string(core) + " at cycle " +
                        std::to_string(cycle) +
                        " pc=" + std::to_string(uint32_t(OUT(debug_pc_out[core]))) +
                        " sp=" + std::to_string(uint32_t(OUT(debug_sp_out[core]))));
                }
                done &= bool(OUT(halted_out[core]));
            }
            // A halted program can still have accepted UART/packet output in flight.
            if (done && !OUT(uart_valid_out) && !OUT(ethernet_tx_valid_out) && tx.empty() &&
                media.drained()) {
                break;
            }
        }
        require(limit == 0 || cycle < limit, "board cycle limit exhausted");
        media.poll();
        require(media.drained(), "Ethernet output still queued at shutdown");
        run_in = false;
        host_mode_in = true;
        for (unsigned bank = 0; bank < EC_BANKS; ++bank) {
            memory_enable_in[bank] = true;
        }
        tick();
        Word result = access(result_address & ~(EC_BITS / 8 - 1), false);
        unsigned code = uint32_t(result >> ((result_address % (EC_BITS / 8)) * 8));
        require(code == 0, "kernel returned " + std::to_string(code));
        require(OUT(device_exit_valid_out) && uint32_t(OUT(device_exit_code_out)) == 0,
                "missing successful MMIO shutdown");
        require(console.find("MIKOS:EXIT 0\n") != std::string::npos, "missing kernel exit marker");
        require(!require_task || launches > 0, "no task executed");
        require(allow_idle_network || socket.empty() || (rx_frames >= 2 && tx_frames >= 2),
                "missing Ethernet traffic");
        std::cout << "Extreme board PASS cycles=" << cycle << " cores=" << EC_CORES
                  << " tasks=" << launches << " rx=" << rx_frames << " tx=" << tx_frames << '\n';
    }
};

int main(int argc, char** argv) {
    try {
        std::string image, socket;
        uint64_t cycles = 10000000, progress = 0;
        bool stalls = false, task = false, allow_idle_network = false;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            if (option == "--stalls") {
                stalls = true;
            } else if (option == "--require-task") {
                task = true;
            } else if (option == "--allow-idle-network") {
                allow_idle_network = true;
            } else {
                require(i + 1 < argc, "option needs a value");
                std::string value = argv[++i];
                if (option == "--image") {
                    image = value;
                } else if (option == "--socket") {
                    socket = value;
                } else if (option == "--cycles") {
                    cycles = std::stoull(value);
                } else if (option == "--progress") {
                    progress = std::stoull(value);
                } else {
                    throw std::runtime_error("unknown board option " + option);
                }
            }
        }
        require(!image.empty(),
                "usage: board --image FILE [--socket PATH] [--cycles N (0=unlimited)] "
                "[--progress N] [--stalls] [--require-task] [--allow-idle-network]");
        Board board;
        board.run(image, socket, cycles, stalls, task, progress, allow_idle_network);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
