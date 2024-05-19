include(FetchContent)

#set(LUA_GIT_HASH a2e0125df529894f5e25d7d477b2df4e37690e0f)

add_compile_definitions(
    "IMGUI_USER_CONFIG=\"${SRC_DIR}/gui/imgui_config.hpp\""
)

FetchContent_Declare(
	rom
	GIT_REPOSITORY https://github.com/xiaoxiao921/ReturnOfModdingBase.git
	GIT_TAG 6f00c5a9840036486b9f1335a3ecfc6afaaff786
)
FetchContent_MakeAvailable(rom)
