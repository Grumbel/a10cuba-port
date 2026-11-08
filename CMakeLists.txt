cmake_minimum_required(VERSION 3.16)
project(a10cuba_port LANGUAGES C)

find_package(SDL3 REQUIRED CONFIG)
add_library(a10-indexed-surface STATIC src/indexed_surface.c)
target_compile_features(a10-indexed-surface PRIVATE c_std_11)
target_compile_options(a10-indexed-surface PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(a10-indexed-surface PUBLIC SDL3::SDL3)
target_include_directories(a10-indexed-surface PUBLIC src)

add_executable(a10-bitmap-viewer src/bitmap_viewer.c)
target_compile_features(a10-bitmap-viewer PRIVATE c_std_11)
target_compile_options(a10-bitmap-viewer PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(a10-bitmap-viewer PRIVATE a10-indexed-surface)

include(CTest)
if(BUILD_TESTING)
    add_executable(test-indexed-surface tests/test_indexed_surface.c)
    target_compile_features(test-indexed-surface PRIVATE c_std_11)
    target_compile_options(test-indexed-surface PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(test-indexed-surface PRIVATE a10-indexed-surface)
    add_test(NAME indexed-surface COMMAND test-indexed-surface)
    set_tests_properties(indexed-surface PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME bitmap-render
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_bitmap_render.py"
                "$<TARGET_FILE:a10-bitmap-viewer>")
endif()
