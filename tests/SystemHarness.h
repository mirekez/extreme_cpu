#pragma once
#include "TestSupport.h"
struct Harness {
    Dut dut;
    bool run_in{};
    bool host_mode_in{};
    bool host_request_in{};
    bool host_write_in{};
    u<32> host_address_in{};
    u<32> debug_register_index_in{};
    u<32> task_debug_id_in{};
    Word host_data_in{};
    bool memory_enable_in[EC_BANKS]{};
    u<32> entry_in[EC_CORES]{};
    Harness() {
#ifndef VERILATOR
        dut.task_debug_id_in=_ASSIGN(task_debug_id_in);
        dut.run_in=_ASSIGN(run_in);
        dut.host_mode_in=_ASSIGN(host_mode_in);
        dut.host_request_in=_ASSIGN(host_request_in);
        dut.host_write_in=_ASSIGN(host_write_in);
        dut.host_address_in=_ASSIGN(host_address_in);
        dut.debug_register_index_in=_ASSIGN(debug_register_index_in);
        dut.host_data_in=_ASSIGN(host_data_in);
        for(unsigned i=0;i<EC_BANKS;++i) dut.memory_enable_in[i]=_ASSIGN_I(memory_enable_in[i]);
        for(unsigned i=0;i<EC_CORES;++i) dut.entry_in[i]=_ASSIGN_I(entry_in[i]);
        dut._assign();
#endif
    }
    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        dut.task_debug_id_in=task_debug_id_in;
        dut.run_in=run_in;
        dut.host_mode_in=host_mode_in;
        dut.host_request_in=host_request_in;
        dut.host_write_in=host_write_in;
        dut.host_address_in=host_address_in;
        dut.debug_register_index_in=debug_register_index_in;
        drive_word(dut.host_data_in,host_data_in);
        for(unsigned i=0;i<EC_BANKS;++i) dut.memory_enable_in[i]=memory_enable_in[i];
        for(unsigned i=0;i<EC_CORES;++i) dut.entry_in[i]=entry_in[i];
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
