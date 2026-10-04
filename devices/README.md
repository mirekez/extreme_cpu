# Extreme board devices

`Peripherals.h` is synthesizable C++HDL. `System.h` includes it when `EC_DEVICES`
is defined, attaching it to the final MemoryMux bank. Other banks remain RAM.
The board uses ordinary CPU loads/stores through the existing FIFOs; no DMA or
interrupt controller is required. Software owns each packet buffer until it
explicitly transfers ownership. Multiple cores must serialize device access.

The MMIO base is `RAM_BANKS * EC_BANK_WORDS * (EC_BITS / 8)`. The device bank
must cover at least 10240 bytes. Control registers occupy separate 64-byte
slots, supporting every bus width. Read/write controls use their lower 32 bits;
control writes must enable all four bytes. Other bytes in a control word are
ignored. Buffer writes use byte enables. Unmapped and malformed requests
return an error with the original transaction tag.

| Offset | Register | Operation |
| --- | --- | --- |
| 0x000 | identity | Read `0x45585431` |
| 0x040 | UART status | Read bit 0: TX ready; bit 1: RX available |
| 0x080 | UART TX | Write one byte; backpressure while occupied |
| 0x0c0 | UART RX | Read and consume one byte, zero if empty |
| 0x100 | timer | Read 32-bit cycle count; wraps naturally |
| 0x140 | RX length | Read completed packet length, zero while empty |
| 0x180 | RX release | Write 1 to release a completed packet |
| 0x1c0 | TX length | Read/write packet length, 1..2048 |
| 0x200 | TX submit | Write 1 to submit the prepared packet |
| 0x240 | network status | Read bit 0: TX buffer available |
| 0x280 | exit code | Write latched simulation shutdown status |
| 0x1000..0x17ff | RX buffer | Read received packet bytes |
| 0x2000..0x27ff | TX buffer | Read/write outgoing packet bytes |

Ethernet stream ports carry raw MAC-client frames, without preamble or FCS.
Each beat contains up to one bus word; only the last may be short. TX remains
stable under backpressure. RX drops malformed/oversized frames through their
last beat, then accepts another frame. This module requires a separate physical
MAC/PHY for a hardware Ethernet link. The simulator uses `sim/Media.h` to move
frames to a rootless AF_UNIX peer; the host never produces guest protocol replies.

`board.cpp` is the shared native/Verilator board harness. It validates image
dimensions, executes all CPU instructions, forwards UART/packets, and checks
the mikOS kernel acceptance markers and final result. `scripts/board.py` builds
the same harness against generated SystemVerilog. CMake builds `ExtremeBoard_cpp`
when `EXTREME_BUILD_DEVICES=ON` (default); `EC_BOARD_BANK_WORDS` sets board capacity.
Interactive launchers may pass `--allow-idle-network` so a session can exit
without receiving host packets. Automated network tests retain the default
requirement for at least two received and two transmitted frames.

```sh
python3 scripts/test.py --test Peripherals --flow all --bits 128
python3 scripts/synth.py --peripherals
# In ~/mikos:
bash tests/extreme/run_kernel.sh --stalls
bash tests/extreme/run_kernel.sh --multicore --verilator --stalls
```

Device regressions check controller response retention, tags/errors, UART
backpressure, simultaneous UART receive/read, packet sizes through 2048 bytes,
masked writes, stream backpressure, malformed-frame recovery and reset.
