# Assembles the release folder and zip. Run through the "ship" target:
#   cmake --build build --config Release --target ship
# Only the files listed here are copied, so a developer's own settings, caches,
# dumps or mods next to the built exe can never end up in a release.
foreach(var BIN SRC OUT VERSION CONFIG)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "ship.cmake: ${var} is not set")
    endif()
endforeach()
if(NOT CONFIG STREQUAL "Release")
    message(FATAL_ERROR "Ship a Release build: cmake --build build --config Release --target ship")
endif()

set(NAME "ReResorep-${VERSION}")
set(DIR "${OUT}/${NAME}")
set(ZIP "${OUT}/${NAME}-win64.zip")

file(REMOVE_RECURSE "${DIR}")
file(REMOVE "${ZIP}" "${ZIP}.sha256.txt")
file(MAKE_DIRECTORY "${DIR}/proxy")

file(COPY "${BIN}/ReResorep.exe" DESTINATION "${DIR}")
file(COPY "${BIN}/proxy/d3d11.dll" DESTINATION "${DIR}/proxy")
configure_file("${SRC}/packaging/README.txt" "${DIR}/README.txt" @ONLY NEWLINE_STYLE CRLF)
configure_file("${SRC}/CREDITS.md" "${DIR}/CREDITS.md" COPYONLY)

file(READ "${SRC}/third_party/imgui/LICENSE.txt" IMGUI_LICENSE)
file(WRITE "${DIR}/THIRD_PARTY_NOTICES.txt"
"ReResorep includes third-party code under the following licences.

==============================================================================
Dear ImGui v1.91.8 - https://github.com/ocornut/imgui
==============================================================================

${IMGUI_LICENSE}")

file(SHA256 "${DIR}/ReResorep.exe" EXE_HASH)
file(SHA256 "${DIR}/proxy/d3d11.dll" DLL_HASH)
# Plain \n: file(WRITE) already writes CRLF on Windows, so an explicit \r would double up.
file(WRITE "${DIR}/SHA256SUMS.txt" "${EXE_HASH}  ReResorep.exe\n${DLL_HASH}  proxy/d3d11.dll\n")

execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${ZIP}" --format=zip "${NAME}"
                WORKING_DIRECTORY "${OUT}" RESULT_VARIABLE ZIP_RESULT)
if(NOT ZIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Creating ${ZIP} failed")
endif()
file(SHA256 "${ZIP}" ZIP_HASH)
file(WRITE "${ZIP}.sha256.txt" "${ZIP_HASH}  ${NAME}-win64.zip\n")

message(STATUS "Shipping folder: ${DIR}")
message(STATUS "Release zip:     ${ZIP}")
message(STATUS "SHA-256 (zip):   ${ZIP_HASH}")
