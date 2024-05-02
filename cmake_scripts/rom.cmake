include(FetchContent)

#set(LUA_GIT_HASH a2e0125df529894f5e25d7d477b2df4e37690e0f)

FetchContent_Declare(
	rom
	GIT_REPOSITORY https://github.com/xiaoxiao921/ReturnOfModdingBase.git
	GIT_TAG 42aa41683c322f40c5a0d71a02add1b3f05e6913
)
FetchContent_MakeAvailable(rom)
