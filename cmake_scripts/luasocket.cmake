include(FetchContent)

project(luasocket LANGUAGES C VERSION 5.2.1)

FetchContent_Declare(
	luasocket
	GIT_REPOSITORY https://github.com/xiaoxiao921/luasocket.git
	GIT_TAG e86e864dc585b65f86769d51a8ed4cc804429040
)
FetchContent_MakeAvailable(luasocket)

set(FILES
    ${luasocket_SOURCE_DIR}/src/luasocket.c
    ${luasocket_SOURCE_DIR}/src/timeout.c
    ${luasocket_SOURCE_DIR}/src/buffer.c
    ${luasocket_SOURCE_DIR}/src/io.c
    ${luasocket_SOURCE_DIR}/src/auxiliar.c
    ${luasocket_SOURCE_DIR}/src/options.c
    ${luasocket_SOURCE_DIR}/src/inet.c
    ${luasocket_SOURCE_DIR}/src/except.c
    ${luasocket_SOURCE_DIR}/src/select.c
    ${luasocket_SOURCE_DIR}/src/tcp.c
    ${luasocket_SOURCE_DIR}/src/udp.c
    ${luasocket_SOURCE_DIR}/src/compat.c
    ${luasocket_SOURCE_DIR}/src/wsocket.c
    ${luasocket_SOURCE_DIR}/src/mime.c
)

add_library(luasocket_static STATIC ${FILES})

target_compile_definitions(luasocket_static PUBLIC $<$<BOOL:${WIN32}>:_CRT_SECURE_NO_WARNINGS>)

find_package(Lua)

target_link_libraries(luasocket_static 
    PRIVATE 
        $<IF:$<BOOL:${LUA_FOUND}>,${LUA_LIBRARIES},lua_static> 
        wsock32 
        ws2_32
)

target_include_directories(luasocket_static
    PUBLIC
        ${luasocket_SOURCE_DIR}/src
    PRIVATE 
        $<$<BOOL:${LUA_FOUND}>:${LUA_INCLUDE_DIR}> # FIXME: It'd be better if lua_static did populate its INTERFACE_INCLUDE_DIRECTORIES property instead
)

set_target_properties(luasocket_static 
    PROPERTIES 
        OUTPUT_NAME luasocket
        PREFIX ""
)

install(
    TARGETS luasocket_static
    RENAME luasocket
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)