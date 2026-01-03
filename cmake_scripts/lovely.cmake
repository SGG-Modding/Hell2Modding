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
    message(STATUS "Could not find lovely_lib static library in any of the expected locations, fetching and building lovely_lib from sources instead")

    message(STATUS "Checking for rustup installation...")
    find_program(RUSTUP_EXECUTABLE rustup)
    if(NOT RUSTUP_EXECUTABLE)
        message(STATUS "Couldn't find rustup, checking directly for rustc instead...")
        find_program(RUSTC_EXECUTABLE rustc)
        if(NOT RUSTC_EXECUTABLE)
            message(FATAL_ERROR "Please make sure rust compiler is correctly installed")
        endif()
    endif()

    include(FetchContent)
    # Corrosion is a tool for integrating Rust into an existing CMake project
    FetchContent_Declare(
        corrosion
        GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
        GIT_TAG v0.6
    )

    FetchContent_Declare(
        lovely_lib
        GIT_REPOSITORY https://github.com/xiaoxiao921/lovely-lib.git
        GIT_TAG 19f5a98430bd09a6a0e337cd9e62d189b9c43f6a
    )
    FetchContent_MakeAvailable(corrosion lovely_lib)

    corrosion_import_crate(MANIFEST_PATH ${lovely_lib_SOURCE_DIR}/Cargo.toml)
    set(LOVELY_LIB "lovely_lib")
endif()
