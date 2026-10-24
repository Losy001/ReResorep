cmake_minimum_required(VERSION 3.20)
project(ReResorep LANGUAGES CXX ASM_MASM RC VERSION 2.2.0)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ReResorep builds for x64 only (configure with -A x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: neither the proxy nor the manager needs a VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)

set(RERESOREP_BIN ${CMAKE_BINARY_DIR}/bin)

# Single source of the version: project(... VERSION x.y.z) above.
set(RERESOREP_GENERATED ${CMAKE_BINARY_DIR}/generated)
configure_file(core/version.h.in ${RERESOREP_GENERATED}/version.h @ONLY)

add_library(resorep_core STATIC
    core/blowfish.cpp
    core/dds.cpp
    core/labels.cpp
    core/mod_package.cpp
    core/mod_state.cpp
    core/resorep_crypto.cpp
    core/settings.cpp
    core/texture_index.cpp
    core/zip.cpp
)
target_include_directories(resorep_core PUBLIC core ${RERESOREP_GENERATED})

add_subdirectory(proxy)
add_subdirectory(manager)

# build/testing: an everyday copy of the app outside the compiler's output folder, so its
# data survives clean rebuilds. Every build refreshes its exe and proxy (part of ALL).
add_custom_target(testing ALL
    COMMAND ${CMAKE_COMMAND} -DBIN=${RERESOREP_BIN} -DDIR=${CMAKE_BINARY_DIR}/testing
            -P ${PROJECT_SOURCE_DIR}/packaging/testing.cmake
    DEPENDS ReResorep proxy
    COMMENT "Updating build/testing"
    VERBATIM)

# Release folder + zip in build/ship: cmake --build build --config Release --target ship
add_custom_target(ship
    COMMAND ${CMAKE_COMMAND} -DBIN=${RERESOREP_BIN} -DSRC=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/ship
            -DVERSION=${PROJECT_VERSION} -DCONFIG=$<CONFIG> -P ${PROJECT_SOURCE_DIR}/packaging/ship.cmake
    DEPENDS ReResorep proxy
    COMMENT "Packaging ReResorep ${PROJECT_VERSION}"
    VERBATIM)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE resorep_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(proxy_test tests/proxy_test.cpp)
target_link_libraries(proxy_test PRIVATE resorep_core)
add_dependencies(proxy_test proxy)
add_test(NAME proxy_test COMMAND proxy_test $<TARGET_FILE:proxy>)

add_executable(manager_test tests/manager_test.cpp manager/library.cpp manager/games.cpp manager/win_util.cpp
    manager/theme.cpp)
target_include_directories(manager_test PRIVATE manager ${RERESOREP_GENERATED})
target_link_libraries(manager_test PRIVATE resorep_core imgui shell32 ole32 version bcrypt)
add_test(NAME manager_test COMMAND manager_test)

add_executable(analyzer_test tests/analyzer_test.cpp manager/analyzer.cpp)
target_include_directories(analyzer_test PRIVATE manager)
target_link_libraries(analyzer_test PRIVATE resorep_core d3d11 d3dcompiler)
add_test(NAME analyzer_test COMMAND analyzer_test)
