include(FetchContent)

project(lpeg LANGUAGES C VERSION 5.2.1)

FetchContent_Declare(
	lpeg
	URL https://www.inf.puc-rio.br/~roberto/lpeg/lpeg-1.1.0.tar.gz
)
FetchContent_MakeAvailable(lpeg)

set(FILES
    ${lpeg_SOURCE_DIR}/lpcap.c
    ${lpeg_SOURCE_DIR}/lpcode.c
    ${lpeg_SOURCE_DIR}/lpcset.c
    ${lpeg_SOURCE_DIR}/lpprint.c
    ${lpeg_SOURCE_DIR}/lptree.c
    ${lpeg_SOURCE_DIR}/lpvm.c
)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
add_library(lpeg_static STATIC ${FILES})
set_target_properties(lpeg_static PROPERTIES OUTPUT_NAME "lpeg")

if (WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
endif ()

find_package(Lua)
if (LUA_FOUND)
    target_link_libraries(lpeg_static PRIVATE ${LUA_LIBRARIES})
    target_include_directories(lpeg_static PRIVATE ${LUA_INCLUDE_DIR})
else()
    target_link_libraries(lpeg_static PRIVATE lua_static)
endif()

set_target_properties(lpeg_static PROPERTIES OUTPUT_NAME lpeg)
set_target_properties(lpeg_static PROPERTIES PREFIX "")
install(
    TARGETS lpeg_static
    RENAME lpeg
    DESTINATION ${CMAKE_INSTALL_BINDIR}
)