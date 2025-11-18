cmake_minimum_required(VERSION 3.20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Hell2Modding CXX C ASM ASM_MASM)

set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# Fetch modules
message("\nFetching modules")
include(cmake_scripts/capstone.cmake)
include(cmake_scripts/eastl.cmake)
include(cmake_scripts/git.cmake)
include(cmake_scripts/lovely.cmake)
include(cmake_scripts/lpeg.cmake)
include(cmake_scripts/luasocket.cmake)
include(cmake_scripts/raw_pdb.cmake)
include(cmake_scripts/rom.cmake)
include(cmake_scripts/safetyhook.cmake)
include(cmake_scripts/tolk.cmake)

message(STATUS "Hell2Modding")
file(GLOB_RECURSE SRC_MAIN
    "${SRC_DIR}/**.hpp"
    "${SRC_DIR}/**.h"
    "${SRC_DIR}/**.cpp"
    "${SRC_DIR}/**.cc"
    "${SRC_DIR}/**.cxx"
    "${SRC_DIR}/**.asm"
    "${SRC_DIR}/**.def"
    "${SRC_DIR}/**.rc"
)

add_compile_options(/bigobj)
add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

if (FINAL)
    add_compile_definitions(FINAL)
endif()

add_library(Hell2Modding MODULE "${SRC_MAIN}")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(TARGET Hell2Modding PROPERTY CXX_STANDARD 23)

source_group(TREE ${SRC_DIR} PREFIX "src" FILES ${SRC_MAIN})

target_include_directories(Hell2Modding PRIVATE
    "${SRC_DIR}"
    "${eastl_SOURCE_DIR}/include"
    "${tolk_SOURCE_DIR}/include"
    "${lpeg_SOURCE_DIR}"
    "${luasocket_SOURCE_DIR}/src"
    "${raw_pdb_SOURCE_DIR}/src"
    "${capstone_SOURCE_DIR}/include/"
)

target_precompile_headers(Hell2Modding PRIVATE "${SRC_DIR}/common.hpp")

target_link_libraries(Hell2Modding PRIVATE ReturnOfModdingBase dxgi dxguid Tolk lpeg_static luasocket_static wsock32 ws2_32 EASTL raw_pdb capstone-static safetyhook Zydis::Zydis ${LOVELY_LIB} userenv ntdll)

set_property(TARGET Hell2Modding PROPERTY COMPILE_WARNING_AS_ERROR ON)

add_compile_definitions(Hell2Modding
    "_CRT_SECURE_NO_WARNINGS"
    "NOMINMAX"
    "WIN32_LEAN_AND_MEAN"
)

if (MSVC)
    if (FINAL)
        # Aggresive inlining (Release)
        string(REPLACE "Ob2" "Ob3" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
        # Aggresive inlining (RelWithDebInfo)
        string(REPLACE "Ob1" "Ob3" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
        # Enable intrinsics
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Oi")
        # Favor fast code
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Ot")
        # -ffast-math (fused multiply-add if the arch allows)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fp:fast")
        # Whole program optimization
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GL")
        # Function-level linking
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Gy")
        # Disable Buffer Security Check
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GS-")
        # Package global data in COMDAT sections for optimization.
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Gw")

        # For all objects:
        # - strip unused functions
        # - fold identical functions
        # - link-time code generation
        string(REPLACE "INCREMENTAL" "INCREMENTAL:NO" CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO}")
        string(REPLACE "INCREMENTAL" "INCREMENTAL:NO" CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO}")
        string(REPLACE "INCREMENTAL" "INCREMENTAL:NO" CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO}")
        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:ICF,REF /LTCG")
        set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} /OPT:ICF,REF /LTCG")
        set(CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO} /OPT:ICF,REF /LTCG")

        set(CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO} /PDBALTPATH:d3d12.pdb")
        set(CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_SHARED_LINKER_FLAGS_RELWITHDEBINFO} /PDBALTPATH:d3d12.pdb")
        set(CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO "${CMAKE_MODULE_LINKER_FLAGS_RELWITHDEBINFO} /PDBALTPATH:d3d12.pdb")
    endif()
endif()

set_target_properties(Hell2Modding PROPERTIES OUTPUT_NAME "d3d12_")
