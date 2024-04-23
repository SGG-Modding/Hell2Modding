project(lua LANGUAGES C VERSION 5.2.1)
if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.24")
    cmake_policy(SET CMP0135 NEW)
endif()
include(FetchContent)

option(LUA_SUPPORT_DL "Support dynamic loading of compiled modules" ON)
option(LUA_BUILD_AS_CXX "Build lua as C++" OFF)
option(LUA_BUILD_BINARY "Build lua binary" OFF)
option(LUA_BUILD_COMPILER "Build luac compiler" OFF)
if(WIN32)
    #add_compile_definitions(LUA_BUILD_AS_DLL)
endif()

FetchContent_Declare(lua_shared
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        c8e96d6e91dc2e3d5b175cc4cd811398ab35c82d # v5.2.2
)
FetchContent_GetProperties(lua_shared)
FetchContent_Populate(lua_shared)

set(LUA_LIB_SRCS
    ${lua_shared_SOURCE_DIR}/lapi.c
    ${lua_shared_SOURCE_DIR}/lauxlib.c
    ${lua_shared_SOURCE_DIR}/lbaselib.c
    ${lua_shared_SOURCE_DIR}/lbitlib.c
    ${lua_shared_SOURCE_DIR}/lcode.c
    ${lua_shared_SOURCE_DIR}/lcorolib.c
    ${lua_shared_SOURCE_DIR}/lctype.c
    ${lua_shared_SOURCE_DIR}/ldblib.c
    ${lua_shared_SOURCE_DIR}/ldebug.c
    ${lua_shared_SOURCE_DIR}/ldo.c
    ${lua_shared_SOURCE_DIR}/ldump.c
    ${lua_shared_SOURCE_DIR}/lfunc.c
    ${lua_shared_SOURCE_DIR}/lgc.c
    ${lua_shared_SOURCE_DIR}/linit.c
    ${lua_shared_SOURCE_DIR}/liolib.c
    ${lua_shared_SOURCE_DIR}/llex.c
    ${lua_shared_SOURCE_DIR}/lmathlib.c
    ${lua_shared_SOURCE_DIR}/lmem.c
    ${lua_shared_SOURCE_DIR}/loadlib.c
    ${lua_shared_SOURCE_DIR}/lobject.c
    ${lua_shared_SOURCE_DIR}/lopcodes.c
    ${lua_shared_SOURCE_DIR}/loslib.c
    ${lua_shared_SOURCE_DIR}/lparser.c
    ${lua_shared_SOURCE_DIR}/lstate.c
    ${lua_shared_SOURCE_DIR}/lstring.c
    ${lua_shared_SOURCE_DIR}/lstrlib.c
    ${lua_shared_SOURCE_DIR}/ltable.c
    ${lua_shared_SOURCE_DIR}/ltablib.c
    ${lua_shared_SOURCE_DIR}/ltm.c
    ${lua_shared_SOURCE_DIR}/lundump.c
    ${lua_shared_SOURCE_DIR}/lvm.c
    ${lua_shared_SOURCE_DIR}/lzio.c
)

if(LUA_BUILD_AS_CXX)
	set_source_files_properties(${LUA_LIB_SRCS} ${lua_shared_SOURCE_DIR}/lua.c ${lua_shared_SOURCE_DIR}/luac.c PROPERTIES LANGUAGE CXX )
endif()

set(CMAKE_SHARED_LIBRARY_PREFIX "")
add_library(lua_shared STATIC ${LUA_LIB_SRCS})
set_target_properties(lua_shared PROPERTIES OUTPUT_NAME "lua52")
target_include_directories(lua_shared PUBLIC ${lua_shared_SOURCE_DIR})
if(UNIX)
    if (UNIX AND NOT(EMSCRIPTEN))
        find_library(LIBM m)
        if(NOT LIBM)
            message(FATAL_ERROR "libm not found and is required by lua")
        endif()
        target_link_libraries(lua_shared PUBLIC ${LIBM})
    endif()
endif()

if (WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

install(TARGETS lua_shared)

set(LUA_INCLUDE_DIR "${lua_shared_SOURCE_DIR}")
set(LUA_LIBRARIES lua_shared)

message("toml++")
FetchContent_Declare(
	toml++
	GIT_REPOSITORY "https://github.com/marzer/tomlplusplus.git"
	GIT_SHALLOW ON
    GIT_SUBMODULES ""
	GIT_TAG "v3.4.0"
)
FetchContent_MakeAvailable(toml++)

message("sol2")
FetchContent_Declare(
	sol2
	GIT_REPOSITORY "https://github.com/ThePhD/sol2.git"
	GIT_SHALLOW ON
    GIT_SUBMODULES ""
	GIT_TAG "v3.3.0"
)
FetchContent_MakeAvailable(sol2)

message("magic_enum")
FetchContent_Declare(
	magic_enum
	GIT_REPOSITORY "https://github.com/Neargye/magic_enum.git"
	GIT_SHALLOW ON
    GIT_SUBMODULES ""
	GIT_TAG "v0.9.5"
)
FetchContent_MakeAvailable(magic_enum)