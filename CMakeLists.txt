cmake_minimum_required(VERSION 3.20)
project(ReResorep LANGUAGES CXX ASM_MASM RC VERSION 2.0.0)

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
target_include_directories(resorep_core PUBLIC core)

add_subdirectory(proxy)
add_subdirectory(manager)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE resorep_core)
add_test(NAME core_tests COMMAND core_tests)

add_executable(proxy_test tests/proxy_test.cpp)
target_link_libraries(proxy_test PRIVATE resorep_core)
add_dependencies(proxy_test proxy)
add_test(NAME proxy_test COMMAND proxy_test $<TARGET_FILE:proxy>)

add_executable(analyzer_test tests/analyzer_test.cpp manager/analyzer.cpp)
target_include_directories(analyzer_test PRIVATE manager)
target_link_libraries(analyzer_test PRIVATE resorep_core d3d11 d3dcompiler)
add_test(NAME analyzer_test COMMAND analyzer_test)
