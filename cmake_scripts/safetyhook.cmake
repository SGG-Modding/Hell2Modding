include(FetchContent)

if(TARGET Zydis AND NOT TARGET Zydis::Zydis)
    add_library(Zydis::Zydis ALIAS Zydis)
    message("added zydis target")
endif()

set(SAFETYHOOK_FETCH_ZYDIS OFF)

FetchContent_Declare(
    safetyhook
    GIT_REPOSITORY https://github.com/cursey/safetyhook.git
    GIT_TAG        1c10b2fe7de7587e6ab1fb4e00bf992bb5793961
)

FetchContent_MakeAvailable(safetyhook)
