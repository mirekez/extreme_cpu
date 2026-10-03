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
Files ending in `.c` use the GNU C11 frontend and define `uint32_t kernel(void)`
without C++ linkage syntax. C and C++ translation units may be linked together.
`--sysroot` supplies target C headers only; no prebuilt target libc is linked.
`--libcxx-include` selects an alternative freestanding libc++ header tree;
the default remains Conda libc++. The entry name is configurable with `--entry`. Multiple translation units,
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
clobbers without push/pop. The compiler sizes a module-wide argument mailbox from its call sites, with a
minimum of eight 32-bit words. A 64-bit argument occupies two words; variadic
64-bit arguments start at even word indices. Scalar return values occupy one or
two words immediately after the argument area. Variadic functions copy incoming
arguments into private activation storage before nested calls can reuse the
mailbox. PHI transfers use temporary
slots to preserve simultaneous assignments. Calls are nonrecursive. Ordinary
scalar modules are not reentrant. For modules issuing
tasks, the compiler provides 33 private context frames (32 tasks plus boot),
including private call mailboxes. The loader parks other cores at an idle HALT;
the hardware task engine can then launch work there. See
[tasks/tasks.h and the task ABI](tasks/README.md).

`--reentrant` enables recursive and indirect calls. It reserves r7 as an
activation-storage pointer and reserves separate storage for each active call.
Each function advances the pointer by its own record size. Frame bases and
record sizes preserve the strongest local alignment required by the module. A bounded call-graph
walk sizes each context for the maximum memory required within the return depth.
Arguments are written into the callee's mailbox before CALL/CALLR, and its result is read before restoring the
caller's storage pointer. Function addresses in globals and local values are
relocated to bus-aligned entry points. COPY saves return registers in the
activation storage so r7 remains intact. This does not change the ISA or move
return addresses into memory.

Dynamic call depth remains bounded by the two reserved return registers:
4, 8, 16 or 32 nested calls for 64, 128, 256 or 512-bit buses. Hardware faults
on overflow. The image entry and task entry functions cannot also be ordinary
callees. A task module reserves a separate set of activation records per task
and for the boot core. Boot storage is sized separately, so large boot-only local
objects are not duplicated across task IDs. Increase image memory when storage
does not fit.
The map records activation and context sizes and marks call depth as dynamic.
The existing static ABI remains the default, including its compile-time recursion/indirect-call diagnostics.

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

Supported operations include scalar integers up to 64 bits, pointers, aggregates
in memory, small struct arguments/returns coerced to one or two i32 words,
direct calls, branches, switches, PHIs, arithmetic, comparisons, and 32-bit
leading-zero/population counts.
Unsupported constructs are diagnosed: recursion and indirect calls without
`--reentrant`, arbitrary vectors, hardware atomics,
thread-local storage, exceptions, and unresolved runtime functions. Allocation,
I/O, threads, global dynamic constructors, and a complete C++ runtime are not
provided by the compiler itself. Variadic functions support `va_start`,
`va_copy`, `va_arg`, and `va_end`, including aligned 64-bit arguments. This is a working freestanding
subset, not full C++ conformance.

The mikOS kernel profile explicitly links compiler-rt C sources for binary32
multiplication, division, unsigned integer conversion and comparison, plus its
integer-only `ceilf`. The compiler passes float values as IEEE-754 bit patterns;
there is no FPU. Missing helper definitions are diagnosed. The mikOS regression
covers this subset, including NaNs, signed zero, subnormals and infinity; a
complete floating-point runtime is not bundled with this compiler.

## Integer 64-bit operations

The ISA still has 32-bit arithmetic. The compiler stores 64-bit integers as two
32-bit words and lowers addition, subtraction, multiplication, division,
remainder, bitwise operations, and shifts through the freestanding C helpers
in `runtime/integer64.c`. The driver links these helpers automatically.
Comparisons, casts, arguments, returns, PHIs, selects, switches, and memory
accesses handle both halves. Signed division truncates toward zero; ordinary
C/C++ undefined cases such as division by zero remain undefined.

With `EC_SIMD` enabled, addition, subtraction, bitwise operations, and constant
logical shifts by 32 use paired 32-bit lanes directly. Multiplication, division,
and variable shifts still use the helpers. This adds no 64-bit arithmetic unit.
A 64-bit volatile access is two ordered 32-bit accesses and is not atomic.
Packed or unaligned accesses use byte operations.

## Regression coverage

CTest builds images for scalar signed/unsigned and packed accesses, nested calls,
copy inside nested calls, library algorithms, alignment/tail copies, and the
bandwidth kernel. The same `tests/compiled.cpp` executes each image against
native C++HDL and generated SystemVerilog through Verilator. Correctness cases
inject controller stalls. Separate negative tests check rejected language/IR
features. RTL tests also exercise return-stack overflow/underflow and malformed
scalar accesses and call targets.
Additional cases cover C input, network byte swaps, compiler-coalesced aggregate
initializers, recursion, global function-pointer tables, COPY across a call,
and simultaneous recursive calls from boot and two tasks using the reentrant
ABI. These execute in both native C++ and Verilator. Integer 64-bit kernels test
both ABIs, signed and unsigned edge cases, carry/borrow, shifts, casts, calls,
and packed memory. A host differential test checks the 32-bit runtime helpers
against native 64-bit arithmetic with undefined-behavior sanitization. SIMD
tests cover explicit carry masks, pair moves, independent 64-bit pairs, and
source/destination aliasing.

## C++ tasks

Include `<tasks/tasks.h>` for issue/disarm, result reads, state/ID, finish, and
abort intrinsics. Task entries are `void()` functions and are relocated to aligned
instruction addresses. The compiler discovers their call graphs and isolates
local storage per task ID, including shared helper functions. See
[tasks/README.md](tasks/README.md) for usage, restrictions, and the dual-memcpy
bandwidth regression.

## Optional SIMD32 intrinsics

Build with `-DEC_SIMD=ON` to compile for SIMD-capable cores. The generated
`extreme-cxx` driver defines `EC_SIMD` for kernels, and the backend recognizes
[SIMD intrinsics](simd/README.md). Disabled builds exclude this API and reject
its external intrinsic symbols. The extension does not enable automatic LLVM
vectorization or arbitrary LLVM vector types.

## Native libc and process contexts

`--frontend armv7` selects Clang's little-endian ILP32 ABI with 64-bit `long double`.
It emits LLVM IR for the Extreme backend; ARM machine code is never linked or
executed. The default frontend remains RV32 ILP32. Do not mix frontend ABIs in
one image. `--lto` additionally optimizes the linked module before legalization.

Binary32 and binary64 arithmetic, comparisons, conversions and non-fused
multiply/add lower through explicitly linked compiler-rt C helpers. Integer
64-bit bit counts, funnel shifts and checked arithmetic use 32-bit instructions.
True fused `llvm.fma` remains unsupported. Package memory moves preserve overlap;
volatile memory intrinsics use ordered byte accesses.

`<context/context.h>` provides save, restore and enter operations with
`--reentrant`. Save returns zero initially; restore resumes it with the supplied
value, mapping zero to one. Enter starts a `void()` entry with a fresh register
return chain and caller-owned activation storage. The entry must not return.
The context contains r0/r1, SP, r7, an aligned continuation and the result-slot
address. It preserves the compiler ABI, not arbitrary assembly registers or
compact LOOP state. Storage must outlive all resumptions. One extra bus word
allows the payload to align internally when libc allocates a jump buffer with
ordinary malloc alignment. Use the header's buffer size rather than hardcoding it.

Variable-sized local allocations use a checked 64 KiB arena per activation
that needs them. Allocations advance an aligned cursor; LLVM stacksave/restore
save and restore that cursor. Overflow faults rather than overwriting adjacent
activations. This is bounded compiler-managed data storage; return addresses
remain in register lanes.

The mikOS image builder tags package globals with `.extreme.user`, reserves
`.extreme.heap` immediately afterward, and places `.extreme.activation` storage
last. Kernel globals stay outside the mutable user region. Private layout
intrinsics expose user-region boundaries and conservative activation sizes to
the image's native loader. `__extreme_activation_end()` reports the current
activation end so fork copies only live activation storage. ELF execution and
process policy belong to the OS, not these compiler intrinsics.
