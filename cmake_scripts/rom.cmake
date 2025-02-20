include(FetchContent)

set(LUA_CUSTOM_REPO https://github.com/xiaoxiao921/lua-fork-hades2.git)

set(LUA_GIT_HASH e2f0a33c52c18516c61b6fedfd6b518c5f0fbdb5)

set(LUA_USE_LUAJIT false)

add_compile_definitions(
    "IMGUI_USER_CONFIG=\"${SRC_DIR}/gui/imgui_config.hpp\""
)

FetchContent_Declare(
	rom
	GIT_REPOSITORY https://github.com/xiaoxiao921/ReturnOfModdingBase.git
	GIT_TAG f580ae6ceb795c2e0b6327bd572d25a6666f5fb3
)
FetchContent_MakeAvailable(rom)
