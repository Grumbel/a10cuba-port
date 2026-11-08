cmake_minimum_required(VERSION 3.16)
project(a10cuba_port LANGUAGES C)

find_package(SDL3 REQUIRED CONFIG)
add_executable(a10-bitmap-viewer src/bitmap_viewer.c)
target_compile_features(a10-bitmap-viewer PRIVATE c_std_11)
target_compile_options(a10-bitmap-viewer PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(a10-bitmap-viewer PRIVATE SDL3::SDL3)

include(CTest)
if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME bitmap-render
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_bitmap_render.py"
                "$<TARGET_FILE:a10-bitmap-viewer>")
endif()
