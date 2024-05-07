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

set(CMAKE_SHARED_LIBRARY_PREFIX "")
add_library(luasocket_static STATIC ${FILES})
set_target_properties(luasocket_static PROPERTIES OUTPUT_NAME "luasocket")

if (WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif ()

find_package(Lua)
if (LUA_FOUND)
    target_link_libraries(luasocket_static PRIVATE ${LUA_LIBRARIES} wsock32 ws2_32)
    target_include_directories(luasocket_static PRIVATE ${LUA_INCLUDE_DIR})
else()
    target_link_libraries(luasocket_static PRIVATE lua_static wsock32 ws2_32)
endif()

set_target_properties(luasocket_static PROPERTIES OUTPUT_NAME luasocket)
set_target_properties(luasocket_static PROPERTIES PREFIX "")
install(
    TARGETS luasocket_static
    RENAME luasocket
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)