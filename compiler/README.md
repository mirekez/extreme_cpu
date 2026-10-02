# Freestanding C++ compiler prototype

`extreme-cxx` uses Conda LLVM 21 Clang to compile C++20 to LLVM IR, then
`extreme-codegen` lowers that IR to the Extreme ISA. This is a small standalone
IR backend, not an upstream LLVM TargetMachine. The frontend uses the RISC-V
ILP32 data layout to establish little-endian 32-bit pointers and integers;
no RISC-V machine instructions are emitted or executed.

Build using the root CMake project (see [setup](../README.TXT)):

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake -j2
build/cmake/bin/extreme-cxx compiler/tests/libraries.cpp -o build/libraries.ecx --emit-ir build/libraries.ll
build/cmake/compiler/Compiled_cpp build/libraries.ecx 205
ctest --test-dir build/cmake --output-on-failure
```

Define `extern "C" uint32_t kernel()` or a void kernel with no arguments.
The entry name is configurable with `--entry`. Multiple translation units,
`-I`, `-D`, and ILP32 `.ll`/`.bc` inputs are accepted. The driver links their IR
before code generation. Images must match the RTL's `--bits`, `--regs`,
`--banks`, and `--bank-words`. The default is 128 bits, eight registers,
two banks, and 4096 words per bank. Driver paths are tied to the CMake build.

## ABI and memory

There is no memory stack. Hardware SP counts occupied 32-bit return-address
lanes in the wide register file. The compiler reserves r0/r1 for those lanes
and sets the stack limit to their combined capacity when calls are present.
It checks the longest direct call chain against that capacity. Entry executes
without a return address. CALL targets and return continuations are bus aligned.
Ordinary scalar programs use r2–r7. Task-enabled modules reserve r7 for
context storage, as described below. In scalar modules COPY preserves r0/r1
in r6/r7 inside called functions; task modules use private frame words instead.

The initial allocator gives SSA values, local objects, and arguments fixed
static memory locations. Live caller values therefore survive callee register
clobbers without push/pop. Up to eight scalar arguments travel through a static
mailbox; return values use its ninth 32-bit slot. PHI transfers use temporary
slots to preserve simultaneous assignments. Calls are nonrecursive. Ordinary
scalar modules are not reentrant. For modules issuing
tasks, the compiler provides 33 private context frames (32 tasks plus boot),
including private call mailboxes. The loader parks other cores at an idle HALT;
the hardware task engine can then launch work there. See
[tasks/tasks.h and the task ABI](tasks/README.md).

Code occupies the lower half of configured memory; globals, static local
objects, spills, and the mailbox occupy the upper half. Layout overflow is an
error. The `.ecx.map` file lists symbols, code/data sizes, call depth, reserved
return slots, and emitted COPY count. The image starts with ten little-endian
32-bit fields: magic `0x58434345`, version 1, bus bits, registers, banks,
words/bank, entry address, idle entry, result address, and payload byte count.
The payload initializes the complete memory. See `tests/compiled.cpp` for loading.

Scalar memory accesses use signed/unsigned byte, halfword, and word flags.
Packed unaligned scalar accesses lower to byte operations. Stores and spills
use conservative completion barriers before subsequent dependent reads. This
prioritizes correctness over scalar performance; register allocation and barrier
scheduling remain optimization work.

## Copies and C++ libraries

Aligned nonoverlapping bulk copies lower to the core's COPY instruction.
Runtime copies use a byte prefix, full-word bulk, and byte tail. Source and
destination with unequal word alignment use the byte path. `memcpy` and
`memset` calls/intrinsics are supported; overlapping `memmove` is not implemented.
The bandwidth fixture copies 1024 words between independent banks, checks every
word, and requires completion within 1088 clocks in both simulation flows.

Clang disables exceptions, RTTI, vectorization, and thread-safe local static
initialization. The tests compile and execute actual Conda libc++ headers for
`std::array`, `std::span`, `std::transform`, and `std::accumulate`. Templates and
exception-free libraries work when their instantiated IR uses supported
operations and supplies any runtime definitions it calls. The small C header
shims under `include/c` provide declarations needed to parse libc++; they are
not implementations of a hosted C library. No host libc/libc++ binary is linked.

Supported operations include scalar integers up to 32 bits, pointers, aggregates
in memory, direct calls, branches, switches, PHIs, arithmetic, and comparisons.
Unsupported constructs are diagnosed: recursion, indirect calls, dynamic alloca,
variadic calls, floating point, computed 64-bit integers, vectors, atomics,
thread-local storage, exceptions, and unresolved runtime functions. Allocation,
I/O, threads, global dynamic constructors, and a complete C++ runtime are not
provided. This is a working freestanding subset, not full C++ conformance.

## Regression coverage

CTest builds images for scalar signed/unsigned and packed accesses, nested calls,
copy inside nested calls, library algorithms, alignment/tail copies, and the
bandwidth kernel. The same `tests/compiled.cpp` executes each image against
native C++HDL and generated SystemVerilog through Verilator. Correctness cases
inject controller stalls. Separate negative tests check rejected language/IR
features. RTL tests also exercise return-stack overflow/underflow and malformed
scalar accesses and call targets.

## C++ tasks

Include `<tasks/tasks.h>` for issue/disarm, result reads, state/ID, finish, and
abort intrinsics. Task entries are `void()` functions and are relocated to aligned
instruction addresses. The compiler discovers their call graphs and isolates
local storage per task ID, including shared helper functions. See
[tasks/README.md](tasks/README.md) for usage, restrictions, and the dual-memcpy
bandwidth regression.
