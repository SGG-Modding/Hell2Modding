include(FetchContent)

FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/aquynh/capstone.git
    GIT_TAG        0f1674223452827493357dd7258145fe9f51cd05
)

set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "Disable shared builds of capstone" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "Disable capstone tests" FORCE)
set(CAPSTONE_X86_ATT_DISABLE ON CACHE BOOL "Disble ATT syntax" FORCE)
FetchContent_MakeAvailable(capstone)
set(CAPSTONE_INCLUDES ${capstone_SOURCE_DIR}/include/)