# Extreme CPU

A C++HDL RTL prototype for cacheless, bandwidth-oriented multicore processing.
It has a custom variable-length ISA, per-core load/store queues, compact loops,
and a streaming copy instruction executed by the core's three stages.

- [Requirements](doc/requirements.md)
- [Architecture and memory protocol](doc/architecture.md)
- [Instruction encodings](doc/instructions.md)
- [Hardware task engine](doc/tasks_engine.md)
- [Compiler and ABI](compiler/README.md)
- [C++ task intrinsics and parallel memcpy](compiler/tasks/README.md)
- [Conda and CMake setup](README.TXT)

Build with `cmake -S . -B build/cmake`, `cmake --build build/cmake -j2`, then
`ctest --test-dir build/cmake --output-on-failure`.

Run `make test` to build and run the **same C++ testbenches** in native C++HDL
and generated-SystemVerilog/Verilator flows. Run `make matrix` for 64-, 256-,
and 512-bit configurations. Requires Python 3, a C++20 compiler, Verilator, and
an already built C++HDL checkout at `~/cpphdl` (override `CPPHDL_HOME`).

```
python3 scripts/test.py --flow cpp
python3 scripts/test.py --flow verilator --test System
python3 scripts/test.py --bits 64 --depth 2 --cores 1 --banks 1
```

Generated RTL, executables, and build logs go under `build/bWIDTH-dDEPTH-cCORES-mBANKS/`.
Set `CXX` and `VERILATOR` to override those tools. There is no handwritten
SystemVerilog implementation. `rtl/System.h` includes synthesizable RAMs and
a host loader for the regression setup; `rtl/Core.h` and `rtl/MemoryMux.h` expose
the interfaces for integrating different memory controllers.

The prototype runs assembled programs and freestanding C++ kernels compiled
with the Conda LLVM 21 frontend and custom IR backend. A production compiler, OS,
physical memory controller, and SIMD ISA are future work. Tests report modeled
copy bandwidth; physical timing, area, and power are not yet characterized.

Run `make synth` for a complete-system synthesis smoke check using Yosys with
the slang frontend. The default synthesis configuration uses a 64-bit bus, one
core, two-entry FIFOs, and a 32-word RAM to keep the gate check small. Override
it with `scripts/synth.py --bits 128 --depth 16 --cores 2 --banks 2 --words 32`.
The script runs synthesis and `check -assert`, and saves the gate netlist and
log under `build/synthesis-*`. This is a logic synthesis check, not placement or
a physical timing result.

Validated configurations (native C++ and Verilator, with matching system cycle
counts):

| Bus bits | FIFO entries | Cores | Memory banks |
|---:|---:|---:|---:|
| 64 | 2 | 1 | 1 |
| 64 | 16 | 2 | 2 |
| 128 | 16 | 2 | 2 |
| 256 | 8 | 2 | 2 |
| 512 | 16 | 2 | 2 |

Coverage includes FIFO wraparound and reordered completions, round-robin
contention, controller stalls, complete-word copying and MOV, compact loops,
all scalar ALU operations, taken/untaken branches, barriers, and fault/reset
behavior. A 1,024-word single-core copy at 128 bits takes 1,063 clocks including
startup and store drain, versus a minimum of 1,024 clocks. A 2,048-word copy
takes 2,087 clocks versus a minimum of 2,048. The system regression
requires `words <= clocks <= words + 64` for large unstalled copies between
independent banks with adequate FIFO depth. Both banks operate concurrently,
with round-robin sharing within each bank. Single-bank and two-entry FIFO
configurations exercise correctness under bandwidth/latency limits separately.
These are simulation measurements.

The compiler regression suite passes at 64, 128, and 512 bits in both flows.
Its 1,024-word C++ memcpy kernel takes 1,085, 1,084, and 1,070 clocks,
respectively, including startup and store drain. The freestanding library test
uses libc++ array/span and algorithms. The compiler currently uses static spill
storage and rejects recursion; see its README for the supported subset.

The shared 32-slot task engine dispatches dependency-ready work to free cores.
Task instructions issue/disarm work, read results, and finish or abort; aborts
cancel dependent continuations. Run `ctest --test-dir build/cmake -R Tasks --output-on-failure` for controller and assembled task-system regressions.

C++ task intrinsics in `compiler/tasks/tasks.h` provide issue, disarm, result
reads, state/ID, finish, and abort. The compiler isolates spills and call mailboxes
per task ID. `tests/memcpy2.cpp` compiles CPU0 issuing two independent memcpy
tasks and tests the complete system with three cores and four single-port banks.
At 128 bits, two 1024-word copies take 1177 clocks and two 2048-word copies take
2201 clocks: exactly one extra clock per extra word in each buffer. Native and
Verilator results match; both flows check copied data, guards, and overlap.

# Prerequisite

Extreme CPU — C++HDL RTL and freestanding C++ compiler

Conda setup (adapted from ~/cpphdl/README.md; that checkout has no README.TXT)

Linux:
  Install Miniconda, then:
  source ~/miniconda3/bin/activate
  conda init

Windows:
  Install MSYS2 and Miniconda and use the MSYS2 console.

Both:
  conda create -p ./.conda
  conda activate ./.conda
  conda env update --file requirements.yaml

Build and test:
  cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build/cmake -j2
  ctest --test-dir build/cmake --output-on-failure

The dependency manifest is copied unchanged from ~/cpphdl/requirements.yaml.
CppHDL must already be built. Override -DCPPHDL_HOME=/path/to/cpphdl if needed.
The compiler defaults to the existing ~/cpphdl/.conda LLVM 21 environment.
For a project-local Conda environment use -DEXTREME_LLVM_ROOT="$PWD/.conda".
BUS_WIDTH, queue depth, and core/bank counts are configured with EC_BITS,
EC_DEPTH, EC_CORES, and EC_BANKS CMake cache variables.

See doc/instructions.md and compiler/README.md for the ISA and compiler ABI.
