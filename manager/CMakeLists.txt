set(IMGUI ${PROJECT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
    ${IMGUI}/imgui.cpp
    ${IMGUI}/imgui_draw.cpp
    ${IMGUI}/imgui_tables.cpp
    ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_win32.cpp
    ${IMGUI}/backends/imgui_impl_dx11.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI} ${IMGUI}/backends)
target_compile_options(imgui PRIVATE /W0)

# Licence texts compiled into the About tab, so they always ship with the exe.
file(READ ${IMGUI}/LICENSE.txt IMGUI_LICENSE)
configure_file(licenses.h.in ${RERESOREP_GENERATED}/licenses.h @ONLY)

add_executable(ReResorep WIN32
    main.cpp
    app.cpp
    games.cpp
    library.cpp
    thumbnails.cpp
    analyzer.cpp
    win_util.cpp
    manager.manifest
    app.rc
)
target_link_libraries(ReResorep PRIVATE resorep_core imgui d3d11 dxgi d3dcompiler shell32 ole32 version dwmapi)
set_target_properties(ReResorep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${RERESOREP_BIN}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${RERESOREP_BIN}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${RERESOREP_BIN}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${RERESOREP_BIN}
)
# The manager ships the proxy next to itself (bin/proxy/d3d11.dll).
add_dependencies(ReResorep proxy)
