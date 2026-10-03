#pragma once
#include "../rtl/Config.h"
#include "registers.h"

// Polling board devices. Ethernet ports carry MAC-client frames without preamble
// or FCS; a physical MAC/PHY or simulator media adapter terminates this stream.
class Peripherals : public Module {
public:
    _PORT(bool) request_in, write_in, response_ready_in, enable_in;
    _PORT(u<32>) address_in, tag_in;
    _PORT(logic<EC_BITS>) data_in;
    _PORT(u<EC_BITS / 8>) mask_in;
    _PORT(bool)
    ready_out = _ASSIGN(
        enable_in() && (!response_valid || response_ready_in()) &&
        !(request_in() && write_in() && address_in() == 128 && uart_pending && !uart_ready_in()));
    _PORT(bool) response_out = _ASSIGN(response_valid != 0);
    _PORT(bool) response_error_out = _ASSIGN(response_error != 0);
    _PORT(u<32>) response_tag_out = _ASSIGN(response_tag);
    _PORT(logic<EC_BITS>) response_data_out = _ASSIGN(response_data);
    _PORT(bool) uart_ready_in, uart_rx_valid_in;
    _PORT(u<8>) uart_rx_data_in;
    _PORT(bool) uart_valid_out = _ASSIGN(uart_pending != 0);
    _PORT(u<8>) uart_data_out = _ASSIGN(uart_data);
    _PORT(bool) uart_rx_ready_out = _ASSIGN(!uart_rx_pending);
    _PORT(bool) rx_valid_in, rx_last_in, tx_ready_in;
    _PORT(logic<EC_BITS>) rx_data_in;
    _PORT(u<32>) rx_count_in;
    _PORT(bool) rx_ready_out = _ASSIGN(!rx_complete);
    _PORT(bool) tx_valid_out = _ASSIGN(tx_active != 0);
    _PORT(logic<EC_BITS>)
    tx_data_out = _ASSIGN(tx_words[(uint32_t)tx_index % (2048 / (EC_BITS / 8))]);
    _PORT(u<32>)
    tx_count_out = _ASSIGN(tx_size - tx_index * (EC_BITS / 8) < EC_BITS / 8
                               ? u<32>(tx_size - tx_index * (EC_BITS / 8))
                               : u<32>(EC_BITS / 8));
    _PORT(bool) tx_last_out = _ASSIGN((tx_index + 1) * (EC_BITS / 8) >= tx_size);
    _PORT(bool) exit_valid_out = _ASSIGN(exit_valid != 0);
    _PORT(u<32>) exit_code_out = _ASSIGN(exit_value);

private:
    reg<u1> response_valid, response_error, uart_pending, uart_rx_pending;
    reg<u1> rx_complete, rx_discard, tx_active, exit_valid;
    reg<u<8>> uart_data, uart_rx_data;
    reg<u<32>> response_tag, ticks, rx_size, rx_index, tx_size, tx_index, exit_value;
    reg<logic<EC_BITS>> response_data;
    reg<logic<EC_BITS>> rx_words[2048 / (EC_BITS / 8)], tx_words[2048 / (EC_BITS / 8)];

public:
    void _assign() {}

    void _work(bool reset) {
        unsigned i, address, index;
        uint32_t value;
        logic<EC_BITS> mask;
        if (reset) {
            response_valid._next = 0;
            response_error._next = 0;
            response_tag._next = 0;
            response_data._next = 0;
            ticks._next = 0;
            uart_pending._next = 0;
            uart_data._next = 0;
            uart_rx_pending._next = 0;
            uart_rx_data._next = 0;
            rx_complete._next = 0;
            rx_discard._next = 0;
            rx_size._next = 0;
            rx_index._next = 0;
            tx_active._next = 0;
            tx_size._next = 0;
            tx_index._next = 0;
            exit_valid._next = 0;
            exit_value._next = 0;
            for (i = 0; i < 2048 / (EC_BITS / 8); ++i) {
                rx_words[i]._next = 0;
                tx_words[i]._next = 0;
            }
        } else {
            ticks._next = ticks + 1;
            if (response_ready_in()) {
                response_valid._next = 0;
            }
            if (uart_pending && uart_ready_in()) {
                uart_pending._next = 0;
            }
            if (uart_rx_valid_in() && uart_rx_ready_out()) {
                uart_rx_pending._next = 1;
                uart_rx_data._next = uart_rx_data_in();
            }
            if (rx_valid_in() && rx_ready_out()) {
                if (rx_discard) {
                    if (rx_last_in()) {
                        rx_discard._next = 0;
                    }
                } else if (rx_count_in() == 0 || rx_count_in() > EC_BITS / 8 ||
                           (!rx_last_in() && rx_count_in() != EC_BITS / 8) ||
                           rx_index >= 2048 / (EC_BITS / 8)) {
                    rx_index._next = 0;
                    rx_discard._next = rx_last_in() ? 0 : 1;
                } else {
                    rx_words[(uint32_t)rx_index]._next = rx_data_in();
                    if (rx_last_in()) {
                        rx_size._next = rx_index * (EC_BITS / 8) + rx_count_in();
                        rx_complete._next = 1;
                        rx_index._next = 0;
                    } else {
                        rx_index._next = rx_index + 1;
                    }
                }
            }
            if (tx_active && tx_ready_in()) {
                if (tx_last_out()) {
                    tx_active._next = 0;
                    tx_index._next = 0;
                } else {
                    tx_index._next = tx_index + 1;
                }
            }
            if (request_in() && ready_out()) {
                address = uint32_t(address_in());
                value = uint32_t(data_in());
                response_valid._next = 1;
                response_tag._next = tag_in();
                response_data._next = 0;
                response_error._next = 0;
                if (address % (EC_BITS / 8) != 0) {
                    response_error._next = 1;
                } else if (address >= 4096 && address < 6144 && !write_in()) {
                    index = (address - 4096) / (EC_BITS / 8);
                    response_data._next = rx_words[index];
                } else if (address >= 8192 && address < 10240) {
                    index = (address - 8192) / (EC_BITS / 8);
                    if (write_in()) {
                        if (tx_active) {
                            response_error._next = 1;
                        } else {
                            mask = 0;
                            for (i = 0; i < EC_BITS / 8; ++i) {
                                mask = mask |
                                       (logic<EC_BITS>((mask_in() >> i) & 1 ? 255 : 0) << (i * 8));
                            }
                            tx_words[index]._next = (tx_words[index] & ~mask) | (data_in() & mask);
                        }
                    } else {
                        response_data._next = tx_words[index];
                    }
                } else if (write_in()) {
                    if ((mask_in() & 15) != 15) {
                        response_error._next = 1;
                    } else if (address == 128) {
                        uart_data._next = value & 255;
                        uart_pending._next = 1;
                    } else if (address == 384 && value == 1) {
                        if (rx_complete) {
                            rx_complete._next = 0;
                            rx_size._next = 0;
                        }
                    } else if (address == 448 && !tx_active && value > 0 && value <= 2048) {
                        tx_size._next = value;
                    } else if (address == 512 && value == 1 && !tx_active && tx_size != 0) {
                        tx_index._next = 0;
                        tx_active._next = 1;
                    } else if (address == 640) {
                        exit_valid._next = 1;
                        exit_value._next = value;
                    } else {
                        response_error._next = 1;
                    }
                } else if (address == 0) {
                    response_data._next = logic<EC_BITS>(0x45585431);
                } else if (address == 64) {
                    response_data._next =
                        logic<EC_BITS>((uart_pending ? 0 : 1) | (uart_rx_pending ? 2 : 0));
                } else if (address == 192) {
                    response_data._next =
                        logic<EC_BITS>(uart_rx_pending ? uint32_t(uart_rx_data) : 0);
                    if (uart_rx_pending) {
                        uart_rx_pending._next = 0;
                    }
                } else if (address == 256) {
                    response_data._next = logic<EC_BITS>(ticks);
                } else if (address == 320) {
                    response_data._next = logic<EC_BITS>(rx_complete ? uint32_t(rx_size) : 0);
                } else if (address == 448) {
                    response_data._next = logic<EC_BITS>(tx_size);
                } else if (address == 576) {
                    response_data._next = logic<EC_BITS>(tx_active ? 0 : 1);
                } else {
                    response_error._next = 1;
                }
            }
        }
    }

    void _strobe() {
        unsigned i;
        response_valid.strobe();
        response_error.strobe();
        response_tag.strobe();
        response_data.strobe();
        ticks.strobe();
        uart_pending.strobe();
        uart_data.strobe();
        uart_rx_pending.strobe();
        uart_rx_data.strobe();
        rx_complete.strobe();
        rx_discard.strobe();
        rx_size.strobe();
        rx_index.strobe();
        tx_active.strobe();
        tx_size.strobe();
        tx_index.strobe();
        exit_valid.strobe();
        exit_value.strobe();
        for (i = 0; i < 2048 / (EC_BITS / 8); ++i) {
            rx_words[i].strobe();
            tx_words[i].strobe();
        }
    }
};
