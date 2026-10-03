Conda setup (following ~/cpphdl/README.md):
  source ~/miniconda3/bin/activate
  conda init
  conda create -p ./.conda
  conda activate ./.conda
  conda env update --file requirements.yaml

Build C++HDL in ~/cpphdl first, then build this project with CMake:
  cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
  cmake --build build/cmake -j2
  ctest --test-dir build/cmake --output-on-failure
CPPHDL_HOME and EXTREME_LLVM_ROOT are CMake paths; the latter defaults to
~/cpphdl/.conda. Use -DEXTREME_LLVM_ROOT="$PWD/.conda" for a local environment.

Optional SIMD32 on every core (default OFF):
  cmake -S . -B build/simd -DEC_SIMD=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build/simd -j2
  ctest --test-dir build/simd --output-on-failure
  cmake --build build/simd --target synth
See compiler/simd/README.md for C++ intrinsics.

Polling device board (UART, timer and CPU-driven Ethernet packet buffers):
  cmake --build build/cmake --target ExtremeBoard_cpp
  python3 scripts/test.py --test Peripherals --flow all
  python3 scripts/synth.py --peripherals
See devices/README.md. The mikOS integration runners are in ~/mikos/tests/extreme;
run_all.sh --kernel-only tests boot and Ethernet in native C++ and Verilator.
Native userspace uses musl, BusyBox and Dropbear on a 512-bit board:
  cd ~/mikos
  python3 tests/extreme/userspace.py --test process
  python3 tests/extreme/userspace.py --test ssh --verilator
See ~/mikos/tests/extreme/README.md for the ABI, build inputs and test-profile limits.
