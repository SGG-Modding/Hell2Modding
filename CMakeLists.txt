cmake_minimum_required(VERSION 3.24)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Hell2Modding CXX C ASM ASM_MASM)


set(CMAKE_POLICY_VERSION_MINIMUM 3.5)


set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# Fetch modules
message(STATUS "\nFetching modules")
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

if(CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    source_group(
        TREE ${SRC_DIR} 
        PREFIX "src" 
        FILES ${SRC_MAIN}
    )
endif()

message(STATUS "Hell2Modding")
add_library(Hell2Modding MODULE "${SRC_MAIN}")

target_compile_options(Hell2Modding
    PUBLIC
        $<$<OR:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>:/bigobj;/utf-8>
)
target_compile_features(Hell2Modding PRIVATE cxx_std_23)
target_compile_definitions(Hell2Modding
    PUBLIC
        $<$<BOOL:${FINAL}>:FINAL>
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
target_precompile_headers(Hell2Modding PRIVATE "${SRC_DIR}/common.hpp")
target_include_directories(Hell2Modding PRIVATE ${SRC_DIR})
target_link_libraries(Hell2Modding 
    PRIVATE 
        ReturnOfModdingBase 
        dxgi 
        dxguid 
        Tolk 
        lpeg_static 
        luasocket_static 
        wsock32 
        ws2_32 
        EASTL 
        raw_pdb 
        capstone 
        safetyhook 
        Zydis::Zydis 
        ${LOVELY_LIB} 
        userenv 
        ntdll
)
set_target_properties(Hell2Modding
    PROPERTIES
        COMPILE_WARNING_AS_ERROR ON
        OUTPUT_NAME "d3d12_"
)

if(MSVC AND FINAL)
    target_compile_options(Hell2Modding
        PUBLIC
            $<$<CONFIG:Release>:/Ob3> # Aggresive inlining (Release)
            $<$<CONFIG:RelWithDebInfo>:/Ob3> # Aggresive inlining (RelWithDebInfo)
            /Oi # Enable intrinsics
            /Ot # Favor fast code
            /fp:fast # -ffast-math (fused multiply-add if the arch allows)
            /GL # Whole program optimization
            /Gy # Function-level linking
            /GS- # Disable Buffer Security Check
            /Gw # Package global data in COMDAT sections for optimization.
    )

    target_link_options(Hell2Modding
        PUBLIC
            $<$<CONFIG:RelWithDebInfo>:
                /INCREMENTAL:NO # Disable incremental linking for space 
                /OPT:ICF,REF # Strip unused functions and fold identical ones
                /LTCG # Link-time code generation
                /PDBALTPATH:d3d12.pdb # Provide alternate PDB path
            >
    )
endif()
