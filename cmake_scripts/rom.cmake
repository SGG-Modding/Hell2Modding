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
	GIT_TAG a14a194bfb8005f5e68a39300408f9db994b3154
)
FetchContent_MakeAvailable(rom)
