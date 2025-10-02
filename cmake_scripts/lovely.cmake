include(FetchContent)

set(LOVELY_LIB_PATHS
    "${CMAKE_SOURCE_DIR}/lovely-lib/target/x86_64-pc-windows-msvc/release/lovely_lib.lib"
    "C:/Users/Quentin/source/repos/lovely-lib/target/release/lovely_lib.lib"
)

set(LOVELY_LIB "")

foreach(lib_path IN LISTS LOVELY_LIB_PATHS)
    message(STATUS "Checking for lovely_lib at ${lib_path}")
    if(EXISTS "${lib_path}")
        set(LOVELY_LIB "${lib_path}")
        message(STATUS "Found lovely_lib at ${lib_path}")
        break()
    endif()
endforeach()

if(NOT LOVELY_LIB)
    message(FATAL_ERROR "Could not find lovely_lib static library in any of the expected locations")
endif()
