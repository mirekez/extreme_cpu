# Compatibility entry points; CMake/CTest owns the build.
.PHONY: all test cpp verilator matrix synth
all:
	cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
	cmake --build build/cmake -j2
test: all
	ctest --test-dir build/cmake --output-on-failure
cpp: all
	ctest --test-dir build/cmake --output-on-failure -E verilator
verilator: all
	ctest --test-dir build/cmake --output-on-failure -R verilator
matrix:
	python3 scripts/test.py --bits 64 --test System
	python3 scripts/test.py --bits 64 --depth 2 --cores 1 --banks 1
	python3 scripts/test.py --bits 256 --depth 8
	python3 scripts/test.py --bits 512 --depth 16
synth:
	python3 scripts/synth.py
