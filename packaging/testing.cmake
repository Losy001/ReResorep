# Refreshes build/testing with the binaries just built in build/bin. Runs on every build
# (the "testing" target). Only ReResorep.exe and proxy/d3d11.dll are replaced; settings,
# mods, dumps and labels in build/testing are never touched.
# If the testing copy is running, the build still succeeds and a warning says so.
foreach(var BIN DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "testing.cmake: ${var} is not set")
    endif()
endforeach()

file(MAKE_DIRECTORY "${DIR}/proxy")
set(stale "")
foreach(file IN ITEMS "ReResorep.exe" "proxy/d3d11.dll")
    file(COPY_FILE "${BIN}/${file}" "${DIR}/${file}" ONLY_IF_DIFFERENT RESULT result)
    if(result)
        list(APPEND stale "${file}")
    endif()
endforeach()

if(stale)
    message(WARNING "build/testing was not updated (${stale} in use). Close ReResorep in build/testing and build again.")
else()
    message(STATUS "build/testing is up to date")
endif()
