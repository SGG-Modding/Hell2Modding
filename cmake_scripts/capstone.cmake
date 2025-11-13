include(FetchContent)

FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/aquynh/capstone.git
    GIT_TAG        8872be6087dd17734d38f290b119f13a56f3027c
)

set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "Disable shared builds of capstone" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "Disable capstone tests" FORCE)
set(CAPSTONE_X86_ATT_DISABLE ON CACHE BOOL "Disble ATT syntax" FORCE)
FetchContent_MakeAvailable(capstone)
set(CAPSTONE_INCLUDES ${capstone_SOURCE_DIR}/include/)