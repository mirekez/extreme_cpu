set(EXTREME_LLVM_ROOT "${CPPHDL_HOME}/.conda" CACHE PATH "LLVM/Clang development environment")
find_package(LLVM REQUIRED CONFIG HINTS "${EXTREME_LLVM_ROOT}/lib/cmake/llvm")
if(NOT LLVM_VERSION_MAJOR EQUAL 21)
    message(FATAL_ERROR "Extreme currently requires LLVM 21")
endif()

find_program(
    EXTREME_CLANG NAMES clang++ clang++-21 HINTS "${EXTREME_LLVM_ROOT}/bin" REQUIRED
    NO_DEFAULT_PATH
)
find_program(
    EXTREME_LLVM_LINK NAMES llvm-link HINTS "${EXTREME_LLVM_ROOT}/bin" REQUIRED NO_DEFAULT_PATH
)
find_program(EXTREME_OPT NAMES opt HINTS "${EXTREME_LLVM_ROOT}/bin" REQUIRED NO_DEFAULT_PATH)
add_executable(extreme-codegen codegen.cpp)
target_include_directories(extreme-codegen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(extreme-codegen PRIVATE LLVM extreme_features)
configure_file(extreme-cxx.py.in ${CMAKE_BINARY_DIR}/bin/extreme-cxx @ONLY)
file(CHMOD ${CMAKE_BINARY_DIR}/bin/extreme-cxx PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
     GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE
)
add_executable(Compiled_cpp ${CMAKE_SOURCE_DIR}/tests/compiled.cpp)
target_link_libraries(Compiled_cpp PRIVATE extreme_rtl)
set(
    case_names
    hello
    scalars
    calls
    libraries
    copy
    calls_copy
    copy_bandwidth
    network
    reentrant
    c_frontend
    kernel_abi
    context
    varargs
    memory_moves
    wide_intrinsics
)
set(
    case_results
    42
    13630
    96
    205
    42
    37
    0
    42
    42
    42
    42
    42
    42
    42
    42
)
if(EC_SIMD)
    list(APPEND case_names simd simd_pairs)
    list(APPEND case_results 42 42)
endif()
set(images)
file(
    GLOB_RECURSE
    target_headers
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*
    ${CMAKE_CURRENT_SOURCE_DIR}/tasks/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/simd/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/context/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime/*
)
foreach(name expected IN ZIP_LISTS case_names case_results)
    set(source ${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.cpp)
    if(name STREQUAL "c_frontend")
        set(source ${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.c)
    endif()
    set(abi_args)
    if(name STREQUAL "reentrant" OR name STREQUAL "context" OR name STREQUAL "varargs")
        set(abi_args --reentrant)
    endif()
    set(image ${CMAKE_CURRENT_BINARY_DIR}/images/${name}.ecx)
    add_custom_command(
        OUTPUT ${image}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${source} -o ${image}
                --bits ${EC_BITS} --banks ${EC_BANKS} ${abi_args}
        DEPENDS extreme-codegen ${source} ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
        VERBATIM
    )
    list(APPEND images ${image})
    if(name STREQUAL "copy_bandwidth")
        if(EC_BANKS GREATER_EQUAL 2 AND EC_DEPTH GREATER_EQUAL 8)
            add_test(NAME compiler.${name}.cpp COMMAND Compiled_cpp ${image} ${expected} 1089 copy)
        endif()
    else()
        add_test(NAME compiler.${name}.cpp COMMAND Compiled_cpp ${image} ${expected} 5000000 stalls)
    endif()
endforeach()
add_custom_target(compiler_images ALL DEPENDS ${images})
add_test(
    NAME compiler.diagnostics
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/diagnostics.py
            ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${EC_SIMD_ENABLED}
)
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/verilator.py
            ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${CPPHDL_HOME} ${CMAKE_CXX_COMPILER} ${EC_BITS}
            ${EC_DEPTH} ${EC_CORES} ${EC_BANKS} ${EC_SIMD_ENABLED}
    )
    set_tests_properties(compiler.verilator PROPERTIES TIMEOUT 600)
endif()

# A dedicated three-core/four-port system supplies two reads and two writes/clock.
add_executable(Memcpy2_cpp ${CMAKE_SOURCE_DIR}/tests/memcpy2.cpp)
target_link_libraries(Memcpy2_cpp PRIVATE extreme_features)
target_include_directories(Memcpy2_cpp PRIVATE ${CPPHDL_HOME}/include)
target_compile_options(Memcpy2_cpp PRIVATE -fno-strict-aliasing)
target_compile_definitions(Memcpy2_cpp PRIVATE EC_BITS=${EC_BITS} EC_DEPTH=16 EC_CORES=3 EC_BANKS=4)
set(memcpy2_images)
foreach(words 1024 2048)
    set(image ${CMAKE_CURRENT_BINARY_DIR}/images/memcpy2_${words}.ecx)
    add_custom_command(
        OUTPUT ${image}
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx
            ${CMAKE_SOURCE_DIR}/tests/memcpy2.cpp -DEXTREME_KERNEL=1 -DEC_COPY_WORDS=${words} --bits
            ${EC_BITS} --banks 4 -o ${image} --emit-ir ${image}.ll
        DEPENDS extreme-codegen ${CMAKE_SOURCE_DIR}/tests/memcpy2.cpp
                ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
        VERBATIM
    )
    list(APPEND memcpy2_images ${image})
endforeach()
add_custom_target(memcpy2_images ALL DEPENDS ${memcpy2_images})
add_test(NAME compiler.memcpy2.cpp COMMAND Memcpy2_cpp ${memcpy2_images})
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.memcpy2.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
            Memcpy2 --bits ${EC_BITS} --depth 16 --cores 3 --banks 4 --image
            ${CMAKE_CURRENT_BINARY_DIR}/images/memcpy2_1024.ecx --image2
            ${CMAKE_CURRENT_BINARY_DIR}/images/memcpy2_2048.ecx --build-dir ${CMAKE_BINARY_DIR}/rtl
            --cxx ${CMAKE_CXX_COMPILER} --cpphdl ${CPPHDL_HOME} ${EC_SIMD_ARGS}
    )
    set_tests_properties(compiler.memcpy2.verilator PROPERTIES TIMEOUT 600)
endif()

set(task_intrinsics_image ${CMAKE_CURRENT_BINARY_DIR}/images/task_intrinsics.ecx)
add_custom_command(
    OUTPUT ${task_intrinsics_image}
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/tasks/tests/intrinsics.cpp --bits ${EC_BITS} --banks 4 -o
        ${task_intrinsics_image} --emit-ir ${task_intrinsics_image}.ll
    DEPENDS extreme-codegen ${CMAKE_CURRENT_SOURCE_DIR}/tasks/tests/intrinsics.cpp
            ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
    VERBATIM
)
add_custom_target(task_intrinsics_image ALL DEPENDS ${task_intrinsics_image})
add_executable(TaskIntrinsics_cpp ${CMAKE_SOURCE_DIR}/tests/compiled.cpp)
target_link_libraries(TaskIntrinsics_cpp PRIVATE extreme_features)
target_include_directories(TaskIntrinsics_cpp PRIVATE ${CPPHDL_HOME}/include)
target_compile_options(TaskIntrinsics_cpp PRIVATE -fno-strict-aliasing)
target_compile_definitions(
    TaskIntrinsics_cpp PRIVATE EC_BITS=${EC_BITS} EC_DEPTH=16 EC_CORES=3 EC_BANKS=4
)
add_test(NAME compiler.task_intrinsics.cpp COMMAND TaskIntrinsics_cpp ${task_intrinsics_image} 42
                                                   5000000 stalls
)
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.task_intrinsics.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
            Compiled --bits ${EC_BITS} --depth 16 --cores 3 --banks 4 --image
            ${task_intrinsics_image} --expected 42 --mode stalls --build-dir
            ${CMAKE_BINARY_DIR}/rtl --cxx ${CMAKE_CXX_COMPILER} --cpphdl ${CPPHDL_HOME}
            ${EC_SIMD_ARGS}
    )
    set_tests_properties(compiler.task_intrinsics.verilator PROPERTIES TIMEOUT 600)
endif()

# Each task owns enough activation records for the bounded hardware call depth.
set(reentrant_tasks_image ${CMAKE_CURRENT_BINARY_DIR}/images/reentrant_tasks.ecx)
add_custom_command(
    OUTPUT ${reentrant_tasks_image}
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/reentrant_tasks.cpp --reentrant --bits ${EC_BITS} --banks
        4 --bank-words 16384 -o ${reentrant_tasks_image}
    DEPENDS extreme-codegen ${CMAKE_CURRENT_SOURCE_DIR}/tests/reentrant_tasks.cpp
            ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
    VERBATIM
)
add_custom_target(reentrant_tasks_image ALL DEPENDS ${reentrant_tasks_image})
add_executable(ReentrantTasks_cpp ${CMAKE_SOURCE_DIR}/tests/compiled.cpp)
target_link_libraries(ReentrantTasks_cpp PRIVATE extreme_features)
target_include_directories(ReentrantTasks_cpp PRIVATE ${CPPHDL_HOME}/include)
target_compile_options(ReentrantTasks_cpp PRIVATE -fno-strict-aliasing)
target_compile_definitions(
    ReentrantTasks_cpp PRIVATE EC_BITS=${EC_BITS} EC_DEPTH=16 EC_CORES=3 EC_BANKS=4
                               EC_BANK_WORDS=16384
)
add_test(NAME compiler.reentrant_tasks.cpp COMMAND ReentrantTasks_cpp ${reentrant_tasks_image} 42
                                                   5000000 stalls
)
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.reentrant_tasks.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
            Compiled --bits ${EC_BITS} --depth 16 --cores 3 --banks 4 --words 16384 --image
            ${reentrant_tasks_image} --expected 42 --mode stalls --build-dir
            ${CMAKE_BINARY_DIR}/rtl --cxx ${CMAKE_CXX_COMPILER} --cpphdl ${CPPHDL_HOME}
            ${EC_SIMD_ARGS}
    )
    set_tests_properties(compiler.reentrant_tasks.verilator PROPERTIES TIMEOUT 600)
endif()

add_executable(Integer64Runtime_cpp runtime/tests/integer64.cpp)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
        Integer64Runtime_cpp PRIVATE -fsanitize=undefined -fno-sanitize-recover=all
    )
    target_link_options(Integer64Runtime_cpp PRIVATE -fsanitize=undefined)
endif()
add_test(NAME compiler.integer64.runtime COMMAND Integer64Runtime_cpp)
set(integer64_images)
foreach(abi static reentrant)
    set(image ${CMAKE_CURRENT_BINARY_DIR}/images/integer64_${abi}.ecx)
    if(abi STREQUAL "reentrant")
        set(abi_args --reentrant)
    else()
        set(abi_args -DEXTREME_STATIC_ABI=1)
    endif()
    add_custom_command(
        OUTPUT ${image}
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/integer64.cpp ${abi_args} --bits ${EC_BITS} --banks 4
            --bank-words 16384 -o ${image}
        DEPENDS extreme-codegen ${CMAKE_CURRENT_SOURCE_DIR}/tests/integer64.cpp
                ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
        VERBATIM
    )
    list(APPEND integer64_images ${image})
    add_test(NAME compiler.integer64.${abi}.cpp COMMAND ReentrantTasks_cpp ${image} 42 20000000
                                                        stalls
    )
endforeach()
add_custom_target(integer64_images ALL DEPENDS ${integer64_images})
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.integer64.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/integer64.py
            ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${CPPHDL_HOME} ${CMAKE_CXX_COMPILER} ${EC_BITS}
            ${EC_SIMD_ENABLED}
    )
    set_tests_properties(compiler.integer64.verilator PROPERTIES TIMEOUT 600)
endif()

# Dynamic local scopes must reclaim their bounded per-activation arena.
set(dynamic_locals_image ${CMAKE_CURRENT_BINARY_DIR}/images/dynamic_locals.ecx)
add_custom_command(
    OUTPUT ${dynamic_locals_image}
    COMMAND
        ${Python3_EXECUTABLE} ${CMAKE_BINARY_DIR}/bin/extreme-cxx
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/dynamic_locals.cpp --reentrant --bits ${EC_BITS} --banks 4
        --bank-words 16384 -o ${dynamic_locals_image}
    DEPENDS extreme-codegen ${CMAKE_CURRENT_SOURCE_DIR}/tests/dynamic_locals.cpp
            ${CMAKE_BINARY_DIR}/bin/extreme-cxx ${target_headers}
    VERBATIM
)
add_custom_target(dynamic_locals_image ALL DEPENDS ${dynamic_locals_image})
add_test(NAME compiler.dynamic_locals.cpp COMMAND ReentrantTasks_cpp ${dynamic_locals_image} 42
                                                  5000000 stalls
)
if(EXTREME_VERILATOR_TESTS)
    add_test(
        NAME compiler.dynamic_locals.verilator
        COMMAND
            ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test.py --flow verilator --test
            Compiled --bits ${EC_BITS} --depth 16 --cores 3 --banks 4 --words 16384 --image
            ${dynamic_locals_image} --expected 42 --mode stalls --build-dir ${CMAKE_BINARY_DIR}/rtl
            --cxx ${CMAKE_CXX_COMPILER} --cpphdl ${CPPHDL_HOME} ${EC_SIMD_ARGS}
    )
    set_tests_properties(compiler.dynamic_locals.verilator PROPERTIES TIMEOUT 600)
endif()
