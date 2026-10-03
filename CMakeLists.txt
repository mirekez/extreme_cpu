cmake_minimum_required(VERSION 3.20)
project(ExtremeCPU LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CPPHDL_HOME "$ENV{HOME}/cpphdl" CACHE PATH "CppHDL checkout")
set(EC_BITS 128 CACHE STRING "Memory bus width in bits")
set(EC_DEPTH 16 CACHE STRING "Per-core FIFO depth")
set(EC_CORES 2 CACHE STRING "Core count")
set(EC_BANKS 2 CACHE STRING "Memory banks")
option(EXTREME_BUILD_COMPILER "Build the LLVM IR compiler" ON)
option(EXTREME_VERILATOR_TESTS "Register generated RTL tests" ON)
option(EC_SIMD "Enable full-register 32-bit SIMD instructions in every core" OFF)
add_library(extreme_features INTERFACE)
set(EC_SIMD_ARGS)
set(EC_SIMD_ENABLED OFF)
if(EC_SIMD)
    set(EC_SIMD_ENABLED ON)
    target_compile_definitions(extreme_features INTERFACE EC_SIMD)
    list(APPEND EC_SIMD_ARGS --simd)
endif()
add_library(extreme_rtl INTERFACE)
target_link_libraries(extreme_rtl INTERFACE extreme_features)
target_include_directories(extreme_rtl INTERFACE "${CPPHDL_HOME}/include")
target_compile_options(extreme_rtl INTERFACE -fno-strict-aliasing)
target_compile_definitions(
    extreme_rtl INTERFACE EC_BITS=${EC_BITS} EC_DEPTH=${EC_DEPTH} EC_CORES=${EC_CORES}
                          EC_BANKS=${EC_BANKS}
)
add_executable(instructions arch/tests/instructions.cpp)
target_link_libraries(instructions PRIVATE extreme_features)
add_test(NAME instructions COMMAND instructions)
foreach(name LoadFifo StoreFifo Memory MemoryMux TasksControl System Tasks SIMD)
    if(name STREQUAL "SIMD")
        set(src tests/simd.cpp)
    elseif(name STREQUAL "Tasks")
        set(src tests/tasks.cpp)
    elseif(name STREQUAL "System")
        set(src tests/memcpy.cpp)
    else()
        set(src rtl/tests/${name}.cpp)
    endif()
    add_executable(${name}_cpp ${src})
    target_link_libraries(${name}_cpp PRIVATE extreme_rtl)
    add_test(NAME ${name}.cpp COMMAND ${name}_cpp)
    if(EXTREME_VERILATOR_TESTS)
        add_test(
            NAME ${name}.verilator
            COMMAND
                ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
                ${name} --bits ${EC_BITS} --depth ${EC_DEPTH} --cores ${EC_CORES} --banks
                ${EC_BANKS} --build-dir ${CMAKE_BINARY_DIR}/rtl --cxx ${CMAKE_CXX_COMPILER}
                --cpphdl ${CPPHDL_HOME} ${EC_SIMD_ARGS}
        )
        set_tests_properties(${name}.verilator PROPERTIES TIMEOUT 300)
    endif()
endforeach()
if(EXTREME_BUILD_COMPILER)
    add_subdirectory(compiler)
endif()
add_custom_target(
    synth COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/synth.py ${EC_SIMD_ARGS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

option(EXTREME_BUILD_DEVICES "Build the UART/Ethernet board simulator" ON)
if(EXTREME_BUILD_DEVICES)
    add_subdirectory(devices)
endif()
