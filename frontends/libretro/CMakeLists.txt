include(FetchContent)
FetchContent_Declare(
    libretro-common
    GIT_REPOSITORY https://github.com/libretro/libretro-common.git
    GIT_TAG master
    SOURCE_SUBDIR nonexistent-headers-only # we only want the headers, not its own CMake build
)
FetchContent_MakeAvailable(libretro-common)

add_library(pokket_libretro MODULE libretro.c)
target_include_directories(pokket_libretro PRIVATE ${libretro-common_SOURCE_DIR}/include)
target_link_libraries(pokket_libretro PRIVATE psemu)
target_compile_definitions(pokket_libretro PRIVATE POKKET_VERSION="${POKKET_VERSION_STRING}")
set_target_properties(pokket_libretro PROPERTIES
    PREFIX ""
    OUTPUT_NAME "pokket_libretro"
)

# CMake gives MODULE libraries a .so suffix on Apple, but libretro (and the
# osx-cmake CI templates, which move $BUILD_DIR/${CORENAME}_libretro.dylib)
# expect .dylib there. Without this the macOS build compiles and links
# cleanly and then fails on the move, which reads as a link failure but
# isn't. Every other platform's default suffix is already correct: .so on
# Linux/webOS, .dll on Windows, and the android template renames the plain
# .so to ${CORENAME}_libretro_android.so itself.
if(APPLE)
    set_target_properties(pokket_libretro PROPERTIES SUFFIX ".dylib")
endif()

# The IR link subsystem. This test includes libretro.c directly, thus it can examine the static state
# of the core. It needs a BIOS dump with the name pocketstation.bin in testdata/, and it skips itself
# with exit code 77 when it finds no dump.
if(PSEMU_BUILD_TESTS)
    add_executable(libretro_link_test libretro_link_test.c)
    target_include_directories(libretro_link_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${libretro-common_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/core/src)
    target_compile_definitions(libretro_link_test PRIVATE
        POKKET_VERSION="${POKKET_VERSION_STRING}"
        PSEMU_TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata")
    target_link_libraries(libretro_link_test PRIVATE psemu)
    add_test(NAME libretro_link_test COMMAND libretro_link_test)
    set_tests_properties(libretro_link_test PROPERTIES SKIP_RETURN_CODE 77)
endif()
