# MicroRT-Lab engine — C++20 deterministic discrete-event simulation engine.
#
# Layout (kept with EXPLICIT source lists so stage agents 2-b2 / 2-b3 can
# simply append their files — never glob):
#   micrort_engine_core : everything except main.cpp (lib: config, schedulers,
#                         CLI, crypto, rng; stage 2 appends the simulation
#                         core: event queue, interpreter, trace, metrics)
#   micrort-engine      : the CLI executable
#   test_*              : in-process unit tests (engine/tests, harness.hpp)

# ---- engine core library (stage 2: append simulation sources here) ----
add_library(micrort_engine_core STATIC
    src/sha256.cpp
    src/rng.cpp
    src/config.cpp
    src/scheduler.cpp
    src/cli.cpp
)
target_include_directories(micrort_engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(micrort_engine_core PUBLIC micrort_kernel)

# ---- CLI executable ----
add_executable(micrort-engine src/main.cpp)
target_link_libraries(micrort-engine PRIVATE micrort_engine_core)

# ---- unit tests (stage 1: config / scheduler / cli; extend the list) ----
set(ENGINE_TESTS
    test_config
    test_scheduler
    test_cli
)
foreach(t ${ENGINE_TESTS})
    add_executable(${t} tests/${t}.cpp)
    target_link_libraries(${t} PRIVATE micrort_engine_core)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 60)
endforeach()
