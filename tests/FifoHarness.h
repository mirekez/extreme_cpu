#pragma once
#include "TestSupport.h"
struct Harness {
    Dut dut;
    bool push_in{};
    bool kind_in{};
    bool pop_in{};
    bool accepted_in{};
    bool response_in{};
    bool response_error_in{};
    u<32> address_in{};
    u<32> response_tag_in{};
    Word data_in{};
    Word response_data_in{};
    Harness() {
#ifndef VERILATOR
        dut.push_in=_ASSIGN(push_in);
        dut.kind_in=_ASSIGN(kind_in);
        dut.pop_in=_ASSIGN(pop_in);
        dut.accepted_in=_ASSIGN(accepted_in);
        dut.response_in=_ASSIGN(response_in);
        dut.response_error_in=_ASSIGN(response_error_in);
        dut.address_in=_ASSIGN(address_in);
        dut.response_tag_in=_ASSIGN(response_tag_in);
        dut.data_in=_ASSIGN(data_in);
        dut.response_data_in=_ASSIGN(response_data_in);
#ifdef TEST_STORE_FIFO
        dut.mask_in=_ASSIGN(u<EC_BITS/8>(~uint64_t(0)));
#endif
        dut._assign();
#endif
    }
    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        dut.push_in=push_in;
        dut.kind_in=kind_in;
        dut.pop_in=pop_in;
        dut.accepted_in=accepted_in;
        dut.response_in=response_in;
        dut.response_error_in=response_error_in;
        dut.address_in=address_in;
        dut.response_tag_in=response_tag_in;
        drive_word(dut.data_in,data_in);
        drive_word(dut.response_data_in,response_data_in);
#ifdef TEST_STORE_FIFO
        dut.mask_in=~uint64_t(0);
#endif
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
