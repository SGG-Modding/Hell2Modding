include(FetchContent)

FetchContent_Declare(
    capstone
    GIT_REPOSITORY https://github.com/aquynh/capstone.git
    GIT_TAG        6.0.0-Alpha6
)

set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "Disable shared builds of capstone" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "Disable capstone tests" FORCE)
set(CAPSTONE_X86_ATT_DISABLE ON CACHE BOOL "Disable ATT syntax" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "Disable all architecture by default" FORCE)
set(CAPSTONE_X86_SUPPORT ON CACHE BOOL "Enable x86 architecture" FORCE)

FetchContent_MakeAvailable(capstone)
