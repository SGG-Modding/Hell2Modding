include(FetchContent)

FetchContent_Declare(
	eastl
	GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
	GIT_TAG 05f4b4aef33f2f3ded08f19fa97f5a27ff35ff9f
	GIT_SUBMODULES_RECURSE  OFF
)
FetchContent_MakeAvailable(eastl)
