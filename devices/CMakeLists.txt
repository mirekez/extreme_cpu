# The final MemoryMux bank is MMIO; EC_BANKS remains the RAM-bank count here.
set(EC_BOARD_BANK_WORDS 16384 CACHE STRING "Words per RAM/MMIO bank in the device board")
math(EXPR EC_BOARD_BANKS "${EC_BANKS} + 1")
add_executable(ExtremeBoard_cpp board.cpp)
target_include_directories(ExtremeBoard_cpp PRIVATE ${CPPHDL_HOME}/include)
target_link_libraries(ExtremeBoard_cpp PRIVATE extreme_features)
target_compile_options(ExtremeBoard_cpp PRIVATE -fno-strict-aliasing)
target_compile_definitions(
    ExtremeBoard_cpp
    PRIVATE EC_DEVICES EC_BITS=${EC_BITS} EC_DEPTH=${EC_DEPTH} EC_CORES=${EC_CORES}
            EC_BANKS=${EC_BOARD_BANKS} EC_BANK_WORDS=${EC_BOARD_BANK_WORDS}
)

add_executable(Peripherals_cpp tests/peripherals.cpp)
target_link_libraries(Peripherals_cpp PRIVATE extreme_rtl)
add_test(NAME Peripherals.cpp COMMAND Peripherals_cpp)
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME Peripherals.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
            Peripherals --bits ${EC_BITS} --cpphdl ${CPPHDL_HOME} --build-dir
            ${CMAKE_BINARY_DIR}/rtl
    )
endif()
