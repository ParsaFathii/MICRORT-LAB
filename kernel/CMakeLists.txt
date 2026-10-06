# MicroRT-Lab kernel — C11 static library + unit tests
add_library(micrort_kernel STATIC
    src/mrt_task.c
    src/mrt_queue.c
    src/mrt_sync.c
    src/mrt_mem.c
    src/mrt_timer.c
    src/mrt_kernel.c
)
target_include_directories(micrort_kernel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---- unit tests (plain C, harness.h asserts) ----
set(KERNEL_TESTS
    test_task
    test_queue
    test_sync
    test_mem
    test_timer
)
foreach(t ${KERNEL_TESTS})
    add_executable(${t} tests/${t}.c)
    target_link_libraries(${t} micrort_kernel)
    add_test(NAME ${t} COMMAND ${t})
    set_tests_properties(${t} PROPERTIES TIMEOUT 30)
endforeach()
