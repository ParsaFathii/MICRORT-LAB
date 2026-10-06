# MicroRT-Lab engine — C++20 discrete-event simulation engine (stub; owned by engine task)
add_executable(micrort-engine src/main.cpp)
target_link_libraries(micrort-engine micrort_kernel)
target_include_directories(micrort-engine PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
