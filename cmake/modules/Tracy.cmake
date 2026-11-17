include(FetchContent)

if(MSVC)
    # MSVC 14.51 rejects Tracy's constexpr ETW source location initializer.
    set(TracyPatchCommand
        PATCH_COMMAND ${CMAKE_COMMAND}
        "-DTRACY_ETW_SOURCE=<SOURCE_DIR>/public/client/windows/TracyETW.cpp"
        -P "${CMAKE_CURRENT_LIST_DIR}/TracyETW.cmake"
    )
endif()

FetchContent_Declare(
    Tracy
    URL https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
    URL_HASH SHA256=bf4af567e9c7524d07f3caa745fad02fb33bd5694f11910750382d1efbb251c1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    ${TracyPatchCommand}
)

set(TRACY_ENABLE 1 CACHE BOOL "" FORCE)
set(TRACY_STATIC OFF CACHE BOOL "" FORCE)
set(TRACY_NO_VSYNC_CAPTURE 1 CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND 1 CACHE BOOL "" FORCE)
set(TRACY_CALLSTACK 1 CACHE BOOL "" FORCE)
set(TRACY_FIBERS 1 CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(Tracy)
