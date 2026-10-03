# Extreme CPU requirements

## Purpose

Develop a CPU with its own instruction set optimized for sustained, high-bandwidth
memory transfers and data processing. A core running a transfer task should
approach the bandwidth efficiency of a DMA controller sharing the same memory
interface, without requiring a separate DMA controller. The CPU must remain
programmable for general scalar algorithms; compatibility with an existing ISA
or existing binaries is not required.

## Required architecture

1. Describe synthesizable RTL in C++ using `~/cpphdl`. Generate SystemVerilog
   from that source; do not maintain a separate hand-written RTL implementation.
2. Support multiple identical cores. Each core owns exactly one load FIFO and
   one store FIFO. Instruction fetch uses the same load FIFO as data reads.
3. Connect those FIFOs directly to memory controllers through registered,
   round-robin arbitration. Support separate controllers serving different
   address regions. Include flow control and completion/error responses.
4. Do not use instruction/data caches, register dependency scoreboards, memory
   dependency detection, store-to-load forwarding, or register bypass networks.
   Queue capacity and stage valid/ready control are required transport control.
5. Use three multicycle stages: load, execute/branch, and store. Each stage may
   take as many clocks as needed. Data transferred between stages is BUS_WIDTH
   bits, accompanied by valid; a stalled downstream stage must retain its data.
6. BUS_WIDTH is configurable from 64 to 512 bits. The initial implementation
   supports 64, 128, 256, and 512. Every instruction-fetch, load, and store
   transaction transfers one aligned BUS_WIDTH word. FIFO payload width is
   BUS_WIDTH; FIFO depth is configurable. Addresses/tags and store byte enables are sideband metadata.
7. Provide a small configurable number N of BUS_WIDTH registers. Implement
   ordinary 32-bit scalar arithmetic, comparisons, branches, loads, and stores,
   with straightforward encodings and full 32-bit immediates.
8. Pack variable-length byte instructions into BUS_WIDTH words. Keep two
   instruction words per core and a byte read position. Issue a prefetch after
   reaching the halfway point of the current word, subject to FIFO capacity.
9. Reserve a strict, compact class of one-byte instructions for loop control,
   pointer iteration, and streaming. Frequent loop bodies must not require
   large branch displacements. General branches may address whole words and
   programs may pad with NOPs.
10. A store becomes visible to reads only when the controller commits it to
    memory. Software/compiler scheduling must respect this rule. Provide an
    explicit barrier that waits for completion, not merely queue acceptance.
11. Support optional SIMD operations over the entire register width on every core.
    Guard the extension with `EC_SIMD` and enable it through the CMake option
    of the same name. The initial extension uses independent 32-bit integer lanes.
    Preserve full-width storage and stage interfaces in the first prototype.

12. Provide RISC-V-style scalar memory size/sign flags operating on the low
    32 register bits, while retaining full-width memory transactions.
13. Store call return addresses in 32-bit register lanes addressed by a small
    SP; make capacity software-limited, use bus-aligned calls, and require no
    memory stack or push/pop instructions.
14. Provide a freestanding C++ compiler using Conda LLVM, initially without
    exceptions or RTTI. Support suitable exception-free template libraries.
    Lower aligned bulk copies to COPY and handle alignment and tails separately.
15. Use CMake/CTest and retain the CppHDL Conda dependency manifest.

16. Provide a hardware task engine with 32 address/predecessor-mask/result
    descriptors, ready-task selection followed by free-core selection, and
    instructions for issue, disarm, result reads, completion, and abort.
    Protect in-flight descriptors and live results, support dependency fan-out
    and joins, and cancel downstream work on explicit abort.

## Verification and acceptance

- Document the architecture and byte-level instruction formats and maintain
  encoding regressions in `arch/tests/instructions.cpp`.
- Independently test load/store queues, memory, and arbitration, including full
  queues, wraparound, stalls, response routing, completion ordering, reset,
  invalid addresses, and fairness under a progressing controller.
- Use the same system-test C++ files for native C++HDL simulation and Verilator
  simulation of generated SystemVerilog. Both flows must check actual data and
  termination, not just compile.
- Exercise scalar math, compact loops, single-core and multicore copies,
  instruction prefetch, memory stalls, and explicit store visibility barriers.
- Large, unstalled single-core copies between independent banks must finish in
  approximately `copied_bytes / (BUS_WIDTH / 8)` clocks. The regression counts
  from core start through HALT, including store completion, and permits at most
  64 additional clocks for setup and drain, using FIFOs of at least eight entries.
- Reads and writes must operate concurrently on independent memory banks while
  preserving per-bank round-robin sharing. A single-port, single-bank copy still
  requires two transactions per word and is checked for correctness separately.

## Prototype boundary

The initial deliverable is an executable, synthesizable CPU prototype, assembler
helper, RAM/controller model, and regressions. A production compiler/backend,
full ABI/runtime, operating system support, interrupts, protection, atomics, and physical
DDR controller are future work. A freestanding LLVM IR compiler prototype supports scalar C++ kernels and
selected exception-free libraries; unsupported features are diagnosed. DMA-equivalent bandwidth is a design
objective measured in the model; area, power, and post-placement clock rate
require later implementation measurements.
