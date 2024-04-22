include(FetchContent)

message("cpp-semver")
FetchContent_Declare(
    cpp-semver
    GIT_REPOSITORY https://github.com/z4kn4fein/cpp-semver.git
    GIT_TAG v0.3.2)
FetchContent_MakeAvailable(cpp-semver)
