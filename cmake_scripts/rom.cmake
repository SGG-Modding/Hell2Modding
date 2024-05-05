include(FetchContent)

#set(LUA_GIT_HASH a2e0125df529894f5e25d7d477b2df4e37690e0f)

FetchContent_Declare(
	rom
	GIT_REPOSITORY https://github.com/xiaoxiao921/ReturnOfModdingBase.git
	GIT_TAG 1667090b021200caa85d201e6a6236a64c1f9df1
)
FetchContent_MakeAvailable(rom)
