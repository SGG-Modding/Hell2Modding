include(FetchContent)

#set(LUA_GIT_HASH a2e0125df529894f5e25d7d477b2df4e37690e0f)

set(LUA_USE_LUAJIT false)

add_compile_definitions(
    "IMGUI_USER_CONFIG=\"${SRC_DIR}/gui/imgui_config.hpp\""
)

FetchContent_Declare(
	rom
	GIT_REPOSITORY https://github.com/xiaoxiao921/ReturnOfModdingBase.git
	GIT_TAG 823d5c21fb093a051e7ed859b352a7e2a881162c
)
FetchContent_MakeAvailable(rom)
