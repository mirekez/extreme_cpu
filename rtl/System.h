#pragma once
#include "Core.h"
#include "MemoryMux.h"
#include "Memory.h"
#include "TasksControl.h"
#ifdef EC_DEVICES
#include "../devices/Peripherals.h"
#endif

class System : public Module {
public:
    Core cores[EC_CORES];
    MemoryMux mux;
    TasksControl tasks;
    _PORT(bool) task_launch_out[EC_CORES], copy_active_out[EC_CORES], copy_word_out[EC_CORES];
    _PORT(u<32>) task_launch_id_out[EC_CORES];
    _PORT(bool) tasks_idle_out = _ASSIGN(tasks.idle_out());
    _PORT(u<32>) task_debug_id_in;
    _PORT(u<32>) task_debug_state_out = _ASSIGN(tasks.debug_state_out());
    _PORT(u<32>) task_debug_result_out = _ASSIGN(tasks.debug_result_out());
    Memory memories[EC_RAM_BANKS];
#ifdef EC_DEVICES
    Peripherals peripherals;
    _PORT(bool)
    uart_ready_in, uart_rx_valid_in, ethernet_rx_valid_in, ethernet_rx_last_in,
        ethernet_tx_ready_in;
    _PORT(u<8>) uart_rx_data_in;
    _PORT(u<32>) ethernet_rx_count_in;
    _PORT(logic<EC_BITS>) ethernet_rx_data_in;
    _PORT(bool) uart_valid_out = _ASSIGN(peripherals.uart_valid_out());
    _PORT(u<8>) uart_data_out = _ASSIGN(peripherals.uart_data_out());
    _PORT(bool) uart_rx_ready_out = _ASSIGN(peripherals.uart_rx_ready_out());
    _PORT(bool) ethernet_rx_ready_out = _ASSIGN(peripherals.rx_ready_out());
    _PORT(bool) ethernet_tx_valid_out = _ASSIGN(peripherals.tx_valid_out());
    _PORT(bool) ethernet_tx_last_out = _ASSIGN(peripherals.tx_last_out());
    _PORT(u<32>) ethernet_tx_count_out = _ASSIGN(peripherals.tx_count_out());
    _PORT(logic<EC_BITS>) ethernet_tx_data_out = _ASSIGN(peripherals.tx_data_out());
    _PORT(bool) device_exit_valid_out = _ASSIGN(peripherals.exit_valid_out());
    _PORT(u<32>) device_exit_code_out = _ASSIGN(peripherals.exit_code_out());
#endif
    _PORT(bool) run_in, host_mode_in, host_request_in, host_write_in;
    _PORT(u<32>) host_address_in;
    _PORT(logic<EC_BITS>) host_data_in;
    _PORT(bool) memory_enable_in[EC_BANKS];
    _PORT(u<32>) entry_in[EC_CORES];
    _PORT(bool) halted_out[EC_CORES], fault_out[EC_CORES];
    _PORT(u<32>) debug_sp_out[EC_CORES];
    _PORT(u<32>) debug_pc_out[EC_CORES];
    _PORT(u<32>) debug_register_index_in;
    _PORT(logic<EC_BITS>) debug_register_out[EC_CORES];
    _PORT(bool) host_ready_out = _ASSIGN_COMB(host_ready_comb_func());
    _PORT(bool) host_response_out = _ASSIGN_COMB(host_response_comb_func());
    _PORT(logic<EC_BITS>) host_result_out = _ASSIGN_COMB(host_result_comb_func());

private:
    bool host_ready_comb, host_response_comb;
    logic<EC_BITS> host_result_comb;

    bool& host_ready_comb_func() {
        unsigned i;
        host_ready_comb = false;
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            if (host_address_in() / (EC_BANK_WORDS * (EC_BITS / 8)) == i) {
                host_ready_comb = host_mode_in() && memories[i].ready_out();
            }
        }
        return host_ready_comb;
    }

    bool& host_response_comb_func() {
        unsigned i;
        host_response_comb = false;
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            if (memories[i].response_out() && host_mode_in()) {
                host_response_comb = true;
            }
        }
        return host_response_comb;
    }

    logic<EC_BITS>& host_result_comb_func() {
        unsigned i;
        host_result_comb = 0;
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            if (memories[i].response_out() && host_mode_in()) {
                host_result_comb = memories[i].response_data_out();
            }
        }
        return host_result_comb;
    }

public:
    void _assign() {
        unsigned i;
        mux._assign();
        tasks.enable_in = _ASSIGN(run_in() && !host_mode_in());
        tasks.debug_id_in = task_debug_id_in;
        tasks._assign();
        for (i = 0; i < EC_CORES; ++i) {
            copy_active_out[i] = cores[i].copy_active_out;
            copy_word_out[i] = cores[i].copy_word_out;
            task_launch_out[i] = tasks.launch_out[i];
            task_launch_id_out[i] = tasks.launch_id_out[i];
            tasks.request_in[i] = cores[i].task_request_out;
            tasks.command_in[i] = cores[i].task_command_out;
            tasks.id_in[i] = cores[i].task_id_out;
            tasks.address_in[i] = cores[i].task_address_out;
            tasks.mask_in[i] = cores[i].task_mask_out;
            tasks.value_in[i] = cores[i].task_value_out;
            tasks.free_in[i] = cores[i].task_free_out;
            tasks.fault_in[i] = cores[i].fault_out;
            cores[i].task_launch_in = tasks.launch_out[i];
            cores[i].task_address_in = tasks.launch_address_out[i];
            cores[i].task_id_in = tasks.launch_id_out[i];
            cores[i].task_accepted_in = tasks.accepted_out[i];
            cores[i].task_response_in = tasks.response_out[i];
            cores[i].task_status_in = tasks.status_out[i];
            cores[i].task_result_in = tasks.result_out[i];
            cores[i].run_in = run_in;
            cores[i].entry_in = entry_in[i];
            cores[i].debug_register_index_in = debug_register_index_in;
            debug_sp_out[i] = cores[i].debug_sp_out;
            debug_pc_out[i] = cores[i].debug_pc_out;
            halted_out[i] = cores[i].halted_out;
            fault_out[i] = cores[i].fault_out;
            debug_register_out[i] = cores[i].debug_register_out;
            mux.mask_in[2 * i] = _ASSIGN(u<EC_BITS / 8>(~uint64_t(0)));
            mux.mask_in[2 * i + 1] = cores[i].store_request_mask_out;
            mux.request_in[2 * i] = cores[i].load_request_out;
            mux.write_in[2 * i] = _ASSIGN(false);
            mux.address_in[2 * i] = cores[i].load_request_address_out;
            mux.tag_in[2 * i] = cores[i].load_request_tag_out;
            mux.data_in[2 * i] = cores[i].load_request_data_out;
            cores[i].load_accepted_in = mux.accepted_out[2 * i];
            cores[i].load_response_in = mux.response_out[2 * i];
            cores[i].load_response_tag_in = mux.response_tag_out[2 * i];
            cores[i].load_response_data_in = mux.response_data_out[2 * i];
            cores[i].load_response_error_in = mux.response_error_out[2 * i];
            mux.request_in[2 * i + 1] = cores[i].store_request_out;
            mux.write_in[2 * i + 1] = _ASSIGN(true);
            mux.address_in[2 * i + 1] = cores[i].store_request_address_out;
            mux.tag_in[2 * i + 1] = cores[i].store_request_tag_out;
            mux.data_in[2 * i + 1] = cores[i].store_request_data_out;
            cores[i].store_accepted_in = mux.accepted_out[2 * i + 1];
            cores[i].store_response_in = mux.response_out[2 * i + 1];
            cores[i].store_response_tag_in = mux.response_tag_out[2 * i + 1];
            cores[i].store_response_data_in = mux.response_data_out[2 * i + 1];
            cores[i].store_response_error_in = mux.response_error_out[2 * i + 1];
            cores[i]._assign();
        }
#ifdef EC_DEVICES
        peripherals.request_in = _ASSIGN(!host_mode_in() && mux.mem_request_out[EC_BANKS - 1]());
        peripherals.write_in = mux.mem_write_out[EC_BANKS - 1];
        peripherals.address_in = mux.mem_address_out[EC_BANKS - 1];
        peripherals.tag_in = mux.mem_tag_out[EC_BANKS - 1];
        peripherals.data_in = mux.mem_data_out[EC_BANKS - 1];
        peripherals.mask_in = mux.mem_mask_out[EC_BANKS - 1];
        peripherals.response_ready_in = mux.mem_response_ready_out[EC_BANKS - 1];
        peripherals.enable_in = memory_enable_in[EC_BANKS - 1];
        mux.mem_ready_in[EC_BANKS - 1] = _ASSIGN(!host_mode_in() && peripherals.ready_out());
        mux.mem_response_in[EC_BANKS - 1] = peripherals.response_out;
        mux.mem_response_tag_in[EC_BANKS - 1] = peripherals.response_tag_out;
        mux.mem_response_data_in[EC_BANKS - 1] = peripherals.response_data_out;
        mux.mem_error_in[EC_BANKS - 1] = peripherals.response_error_out;
        peripherals.uart_ready_in = uart_ready_in;
        peripherals.uart_rx_valid_in = uart_rx_valid_in;
        peripherals.uart_rx_data_in = uart_rx_data_in;
        peripherals.rx_valid_in = ethernet_rx_valid_in;
        peripherals.rx_last_in = ethernet_rx_last_in;
        peripherals.rx_count_in = ethernet_rx_count_in;
        peripherals.rx_data_in = ethernet_rx_data_in;
        peripherals.tx_ready_in = ethernet_tx_ready_in;
        peripherals._assign();
#endif
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            memories[i].request_in = _ASSIGN_I(
                host_mode_in() ? (host_request_in() &&
                                  host_address_in() / (EC_BANK_WORDS * (EC_BITS / 8)) == i)
                               : mux.mem_request_out[i]());
            memories[i].write_in =
                _ASSIGN_I(host_mode_in() ? host_write_in() : mux.mem_write_out[i]());
            memories[i].address_in = _ASSIGN_I(
                host_mode_in() ? uint32_t(host_address_in() % (EC_BANK_WORDS * (EC_BITS / 8)))
                               : uint32_t(mux.mem_address_out[i]()));
            memories[i].tag_in = _ASSIGN_I(host_mode_in() ? u<32>(0) : mux.mem_tag_out[i]());
            memories[i].data_in =
                _ASSIGN_I(host_mode_in() ? host_data_in() : mux.mem_data_out[i]());
            memories[i].response_ready_in =
                _ASSIGN_I(host_mode_in() || mux.mem_response_ready_out[i]());
            memories[i].mask_in =
                _ASSIGN_I(host_mode_in() ? u<EC_BITS / 8>(~uint64_t(0)) : mux.mem_mask_out[i]());
            memories[i].enable_in = memory_enable_in[i];
            mux.mem_ready_in[i] = _ASSIGN_I(!host_mode_in() && memories[i].ready_out());
            mux.mem_response_in[i] = _ASSIGN_I(!host_mode_in() && memories[i].response_out());
            mux.mem_response_tag_in[i] = memories[i].response_tag_out;
            mux.mem_response_data_in[i] = memories[i].response_data_out;
            mux.mem_error_in[i] = memories[i].response_error_out;
            memories[i]._assign();
        }
    }

    void _work(bool reset) {
        unsigned i;
        for (i = 0; i < EC_CORES; ++i) {
            cores[i]._work(reset);
        }
        mux._work(reset);
        tasks._work(reset);
#ifdef EC_DEVICES
        peripherals._work(reset);
#endif
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            memories[i]._work(reset);
        }
    }

    void _strobe() {
        unsigned i;
        for (i = 0; i < EC_CORES; ++i) {
            cores[i]._strobe();
        }
        mux._strobe();
        tasks._strobe();
#ifdef EC_DEVICES
        peripherals._strobe();
#endif
        for (i = 0; i < EC_RAM_BANKS; ++i) {
            memories[i]._strobe();
        }
    }
};
