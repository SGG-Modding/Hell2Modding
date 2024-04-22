include(FetchContent)

FetchContent_Declare(
	polyhook
	GIT_REPOSITORY https://github.com/stevemk14ebr/PolyHook_2_0.git
	GIT_TAG fd2a88f09c8ae89440858fc52573656141013c7f
)
FetchContent_MakeAvailable(polyhook)