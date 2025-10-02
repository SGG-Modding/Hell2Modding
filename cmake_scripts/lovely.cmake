include(FetchContent)

# Path to Rust staticlib
set(LOVELY_LIB_DIR "${CMAKE_SOURCE_DIR}/target/x86_64-pc-windows-msvc/release")

find_library(LOVELY_LIB lovely_lib PATHS ${LOVELY_LIB_DIR} NO_DEFAULT_PATH)

if (NOT LOVELY_LIB)
    message(FATAL_ERROR "Could not find lovely_lib static library")
endif()
