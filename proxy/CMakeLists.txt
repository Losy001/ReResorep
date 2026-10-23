# Generate the export stubs, .def and name table from the single list in exports.txt.
file(STRINGS exports.txt PASSTHROUGH_EXPORTS)
set(STUBS "")
set(DEF_EXPORTS "")
set(NAME_TABLE "")
set(INDEX 0)
foreach(name IN LISTS PASSTHROUGH_EXPORTS)
    string(APPEND STUBS "PASSTHROUGH ${name}, ${INDEX}\n")
    string(APPEND DEF_EXPORTS "    ${name} = ${name}_stub\n")
    string(APPEND NAME_TABLE "\"${name}\",\n")
    math(EXPR INDEX "${INDEX} + 1")
endforeach()

set(GEN ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(stubs_common.asm.in ${GEN}/stubs.asm @ONLY)
file(CONFIGURE OUTPUT ${GEN}/passthrough_names.inc CONTENT "${NAME_TABLE}")
file(CONFIGURE OUTPUT ${GEN}/d3d11.def CONTENT
"LIBRARY d3d11
EXPORTS
    D3D11CreateDevice
    D3D11CreateDeviceAndSwapChain
    ReResorepProxyVersion
${DEF_EXPORTS}")

add_library(proxy SHARED
    proxy_main.cpp
    runtime.cpp
    texture_hook.cpp
    proxy.rc
    ${GEN}/stubs.asm
    ${GEN}/d3d11.def
)
target_include_directories(proxy PRIVATE ${GEN} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(proxy PRIVATE resorep_core)
set_target_properties(proxy PROPERTIES
    OUTPUT_NAME d3d11
    RUNTIME_OUTPUT_DIRECTORY ${RERESOREP_BIN}/proxy
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${RERESOREP_BIN}/proxy
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${RERESOREP_BIN}/proxy
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${RERESOREP_BIN}/proxy
)
