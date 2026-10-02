#pragma once
#include "TestSupport.h"

struct Harness {
    Dut dut;
    bool request_in[2 * EC_CORES]{};
    bool write_in[2 * EC_CORES]{};
    u<32> address_in[2 * EC_CORES]{};
    u<32> tag_in[2 * EC_CORES]{};
    Word data_in[2 * EC_CORES]{};
    bool mem_ready_in[EC_BANKS]{};
    bool mem_response_in[EC_BANKS]{};
    bool mem_error_in[EC_BANKS]{};
    u<32> mem_response_tag_in[EC_BANKS]{};
    Word mem_response_data_in[EC_BANKS]{};

    Harness() {
#ifndef VERILATOR
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.request_in[i] = _ASSIGN_I(request_in[i]);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.write_in[i] = _ASSIGN_I(write_in[i]);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.address_in[i] = _ASSIGN_I(address_in[i]);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.tag_in[i] = _ASSIGN_I(tag_in[i]);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.data_in[i] = _ASSIGN_I(data_in[i]);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.mask_in[i] = _ASSIGN(u<EC_BITS / 8>(~uint64_t(0)));
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_ready_in[i] = _ASSIGN_I(mem_ready_in[i]);
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_response_in[i] = _ASSIGN_I(mem_response_in[i]);
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_error_in[i] = _ASSIGN_I(mem_error_in[i]);
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_response_tag_in[i] = _ASSIGN_I(mem_response_tag_in[i]);
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_response_data_in[i] = _ASSIGN_I(mem_response_data_in[i]);
        }
        dut._assign();
#endif
    }

    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.request_in[i] = request_in[i];
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.write_in[i] = write_in[i];
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.mask_in[i] = ~uint64_t(0);
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.address_in[i] = address_in[i];
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            dut.tag_in[i] = tag_in[i];
        }
        for (unsigned i = 0; i < 2 * EC_CORES; ++i) {
            drive_word(dut.data_in[i], data_in[i]);
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_ready_in[i] = mem_ready_in[i];
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_response_in[i] = mem_response_in[i];
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_error_in[i] = mem_error_in[i];
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            dut.mem_response_tag_in[i] = mem_response_tag_in[i];
        }
        for (unsigned i = 0; i < EC_BANKS; ++i) {
            drive_word(dut.mem_response_data_in[i], mem_response_data_in[i]);
        }
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
};
