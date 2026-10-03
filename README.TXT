
Optional SIMD32 on every core (default OFF):
  cmake -S . -B build/simd -DEC_SIMD=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build/simd -j2
  ctest --test-dir build/simd --output-on-failure
  cmake --build build/simd --target synth
See compiler/simd/README.md for C++ intrinsics.
