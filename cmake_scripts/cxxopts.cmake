include(FetchContent)

message("cxxopts")
FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG v3.1.1
    GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(cxxopts)

set_property(TARGET cxxopts PROPERTY CXX_STANDARD 23)
