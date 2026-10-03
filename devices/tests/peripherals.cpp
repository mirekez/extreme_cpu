#include "../Peripherals.h"
Peripherals top;
#ifndef SYNTHESIS
#define TEST_TOP Peripherals
#include "../../tests/TestSupport.h"
#include "../registers.h"
using namespace extreme::devices;

struct DeviceTest {
    Dut dut;
    bool request_in{};
    bool write_in{};
    bool response_ready_in{};
    bool enable_in{};
    bool uart_ready_in{};
    bool uart_rx_valid_in{};
    bool rx_valid_in{};
    bool rx_last_in{};
    bool tx_ready_in{};
    u<32> address_in{};
    u<32> tag_in{};
    u<32> rx_count_in{};
    Word data_in{};
    Word rx_data_in{};
    u<EC_BITS / 8> mask_in{};
    u<8> uart_rx_data_in{};

    DeviceTest() {
#ifndef VERILATOR
        dut.request_in = _ASSIGN(request_in);
        dut.write_in = _ASSIGN(write_in);
        dut.response_ready_in = _ASSIGN(response_ready_in);
        dut.enable_in = _ASSIGN(enable_in);
        dut.uart_ready_in = _ASSIGN(uart_ready_in);
        dut.uart_rx_valid_in = _ASSIGN(uart_rx_valid_in);
        dut.rx_valid_in = _ASSIGN(rx_valid_in);
        dut.rx_last_in = _ASSIGN(rx_last_in);
        dut.tx_ready_in = _ASSIGN(tx_ready_in);
        dut.address_in = _ASSIGN(address_in);
        dut.tag_in = _ASSIGN(tag_in);
        dut.rx_count_in = _ASSIGN(rx_count_in);
        dut.data_in = _ASSIGN(data_in);
        dut.rx_data_in = _ASSIGN(rx_data_in);
        dut.mask_in = _ASSIGN(mask_in);
        dut.uart_rx_data_in = _ASSIGN(uart_rx_data_in);
        dut._assign();
#endif
    }

    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        dut.request_in = request_in;
        dut.write_in = write_in;
        dut.response_ready_in = response_ready_in;
        dut.enable_in = enable_in;
        dut.uart_ready_in = uart_ready_in;
        dut.uart_rx_valid_in = uart_rx_valid_in;
        dut.rx_valid_in = rx_valid_in;
        dut.rx_last_in = rx_last_in;
        dut.tx_ready_in = tx_ready_in;
        dut.address_in = address_in;
        dut.tag_in = tag_in;
        dut.rx_count_in = rx_count_in;
        drive_word(dut.data_in, data_in);
        drive_word(dut.rx_data_in, rx_data_in);
        dut.mask_in = mask_in;
        dut.uart_rx_data_in = uart_rx_data_in;
        dut.clk = 0;
        dut.eval();
#endif
    }

    void tick(bool reset = false) {
#ifdef VERILATOR
        dut.reset = reset;
#endif
        settle();
#ifdef VERILATOR
        dut.clk = 1;
        dut.eval();
#else
        dut._work(reset);
        dut._strobe();
#endif
        ++_system_clock;
    }

    Word access(unsigned address, bool write = false, Word value = 0, bool error = false,
                uint64_t mask = ~uint64_t(0)) {
        address_in = address;
        write_in = write;
        data_in = value;
        mask_in = mask;
        tag_in = 0x12345678;
        request_in = true;
        response_ready_in = true;
        settle();
        require(OUT(ready_out), "device request not accepted");
        tick();
        request_in = false;
        settle();
        require(OUT(response_out), "device response missing");
        require(bool(OUT(response_error_out)) == error, "device error mismatch");
        require(uint32_t(OUT(response_tag_out)) == 0x12345678, "device response tag");
        Word result = read_word(OUT(response_data_out));
        tick();
        return result;
    }

    void run() {
        enable_in = true;
        response_ready_in = true;
        tick(true);
        tick(true);
        require(uint32_t(access(identity)) == device_id, "device identity");
        uint32_t before = uint32_t(access(timer));
        for (unsigned i = 0; i < 5; ++i) {
            tick();
        }
        require(uint32_t(access(timer)) > before, "timer does not advance");
        access(identity, true, 0, true);
        access(3, false, 0, true);
        access(0x800, false, 0, true);
        access(tx_length, true, 0, true);
        access(tx_length, true, packet_bytes + 1, true);
        access(tx_length, true, 64, true, 1);
        access(uart_tx, true, 'X');
        settle();
        require(OUT(uart_valid_out) && uint32_t(OUT(uart_data_out)) == 'X', "UART output");
        request_in = true;
        for (unsigned cycle = 0; cycle < 4; ++cycle) {
            settle();
            require(!OUT(ready_out), "UART should backpressure a second write");
            tick();
        }
        request_in = false;
        uart_ready_in = true;
        tick();
        uart_rx_data_in = 'Y';
        uart_rx_valid_in = true;
        tick();
        uart_rx_valid_in = false;
        require(uint32_t(access(uart_status)) == 3, "UART status");
        require(uint32_t(access(uart_rx)) == 'Y', "UART RX data");
        require(uint32_t(access(uart_status)) == 1, "UART RX consumption");
        // Reading an empty RX register on an arriving-byte edge must not lose that byte.
        address_in = uart_rx;
        request_in = true;
        write_in = false;
        uart_rx_data_in = 'Z';
        uart_rx_valid_in = true;
        tick();
        request_in = false;
        uart_rx_valid_in = false;
        require(uint32_t(access(uart_rx)) == 'Z', "UART simultaneous receive/read lost data");
        // Register response must remain stable while the controller is stalled.
        response_ready_in = false;
        request_in = true;
        write_in = false;
        address_in = identity;
        tick();
        request_in = false;
        for (unsigned cycle = 0; cycle < 4; ++cycle) {
            settle();
            require(OUT(response_out) && uint32_t(read_word(OUT(response_data_out))) == device_id,
                    "response did not hold");
            require(!OUT(ready_out), "response backpressure");
            tick();
        }
        response_ready_in = true;
        tick();
        for (unsigned size : {1u, unsigned(EC_BITS / 8), 554u, packet_bytes}) {
            std::vector<uint8_t> packet(size);
            for (unsigned byte = 0; byte < size; ++byte) {
                packet[byte] = uint8_t(byte * 37 + size);
            }
            for (unsigned offset = 0; offset < size; offset += EC_BITS / 8) {
                Word word = 0;
                unsigned count = std::min(unsigned(EC_BITS / 8), size - offset);
                for (unsigned byte = 0; byte < count; ++byte) {
                    word.bits(byte * 8 + 7, byte * 8) = packet[offset + byte];
                }
                rx_data_in = word;
                rx_count_in = count;
                rx_last_in = offset + count == size;
                rx_valid_in = true;
                settle();
                require(OUT(rx_ready_out), "RX stream blocked before frame completion");
                tick();
            }
            rx_valid_in = false;
            settle();
            require(!OUT(rx_ready_out), "completed RX frame must stay owned by CPU");
            require(uint32_t(access(rx_length)) == size, "RX length");
            for (unsigned offset = 0; offset < size; offset += EC_BITS / 8) {
                Word word = access(rx_buffer + offset);
                for (unsigned byte = 0; byte < EC_BITS / 8 && offset + byte < size; ++byte) {
                    require(uint8_t(uint32_t(word >> (byte * 8))) == packet[offset + byte],
                            "RX payload");
                }
                access(tx_buffer + offset, true, word);
            }
            access(rx_release, true, 1);
            require(uint32_t(access(rx_length)) == 0, "RX release");
            access(tx_length, true, size);
            access(tx_submit, true, 1);
            access(tx_buffer, true, 0, true);
            access(tx_submit, true, 1, true);
            unsigned offset = 0;
            while (offset < size) {
                tx_ready_in = false;
                settle();
                require(OUT(tx_valid_out), "TX frame missing");
                Word word = read_word(OUT(tx_data_out));
                unsigned count = uint32_t(OUT(tx_count_out));
                for (unsigned cycle = 0; cycle < 3; ++cycle) {
                    tick();
                    settle();
                    require(read_word(OUT(tx_data_out)) == word, "TX word changed during stall");
                }
                require(count == std::min(unsigned(EC_BITS / 8), size - offset), "TX tail count");
                require(bool(OUT(tx_last_out)) == (offset + count == size), "TX last");
                for (unsigned byte = 0; byte < count; ++byte) {
                    require(uint8_t(uint32_t(word >> (byte * 8))) == packet[offset + byte],
                            "TX payload");
                }
                tx_ready_in = true;
                tick();
                offset += count;
            }
            tx_ready_in = false;
            require(uint32_t(access(net_status)) == 1, "TX completion");
        }
        // A malformed beat drops the entire frame through its last beat.
        rx_count_in = 1;
        rx_last_in = false;
        rx_valid_in = true;
        tick();
        rx_last_in = true;
        tick();
        rx_valid_in = false;
        require(uint32_t(access(rx_length)) == 0, "malformed RX tail was published");
        rx_valid_in = true;
        tick();
        rx_valid_in = false;
        require(uint32_t(access(rx_length)) == 1, "RX did not recover after a malformed frame");
        access(rx_release, true, 1);
        access(tx_buffer, true, pattern(99));
        access(tx_buffer, true, 0, false, 1);
        Word expected = pattern(99);
        expected.bits(7, 0) = 0;
        require(access(tx_buffer) == expected, "packet byte mask");
        access(exit_code, true, 42);
        settle();
        require(OUT(exit_valid_out) && uint32_t(OUT(exit_code_out)) == 42, "exit status");
        tick(true);
        settle();
        require(!OUT(exit_valid_out) && !OUT(tx_valid_out) && !OUT(uart_valid_out), "device reset");
        std::cout << "Peripherals PASS bits=" << EC_BITS << "\n";
    }
};

int main() {
    DeviceTest test;
    test.run();
}
#endif
