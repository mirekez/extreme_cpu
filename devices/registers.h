#pragma once
#include <stdint.h>

// Byte offsets within the final MemoryMux bank. Registers are 64 bytes apart
// so each occupies its own bus word for every supported EC_BITS configuration.
namespace extreme::devices {
constexpr uint32_t identity = 0x000;
constexpr uint32_t uart_status = 0x040;
constexpr uint32_t uart_tx = 0x080;
constexpr uint32_t uart_rx = 0x0c0;
constexpr uint32_t timer = 0x100;
constexpr uint32_t rx_length = 0x140;
constexpr uint32_t rx_release = 0x180;
constexpr uint32_t tx_length = 0x1c0;
constexpr uint32_t tx_submit = 0x200;
constexpr uint32_t net_status = 0x240;
constexpr uint32_t exit_code = 0x280;
constexpr uint32_t rx_buffer = 0x1000;
constexpr uint32_t tx_buffer = 0x2000;
constexpr uint32_t packet_bytes = 2048;
constexpr uint32_t device_id = 0x45585431;
} // namespace extreme::devices
