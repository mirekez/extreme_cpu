#pragma once
#include "TestSupport.h"
struct Harness {
    Dut dut;
    bool request_in{};
    bool write_in{};
    bool response_ready_in{};
    bool enable_in{};
    u<32> address_in{};
    u<32> tag_in{};
    u<EC_BITS/8> mask_in{~uint64_t(0)};
    Word data_in{};
    Harness() {
#ifndef VERILATOR
        dut.request_in=_ASSIGN(request_in);
        dut.write_in=_ASSIGN(write_in);
        dut.response_ready_in=_ASSIGN(response_ready_in);
        dut.enable_in=_ASSIGN(enable_in);
        dut.address_in=_ASSIGN(address_in);
        dut.tag_in=_ASSIGN(tag_in);
        dut.data_in=_ASSIGN(data_in);
        dut.mask_in=_ASSIGN(mask_in);
        dut._assign();
#endif
    }
    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        dut.request_in=request_in;
        dut.write_in=write_in;
        dut.mask_in=mask_in;
        dut.response_ready_in=response_ready_in;
        dut.enable_in=enable_in;
        dut.address_in=address_in;
        dut.tag_in=tag_in;
        drive_word(dut.data_in,data_in);
        dut.clk=0; dut.eval();
#endif
    }
    void tick(bool reset=false) {
#ifdef VERILATOR
        dut.reset=reset;
#endif
        settle();
#ifdef VERILATOR
        dut.clk=1; dut.eval();
#else
        dut._work(reset); dut._strobe();
#endif
        ++_system_clock;
    }
};
