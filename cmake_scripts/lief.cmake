include(FetchContent)

FetchContent_Declare(lief
    GIT_REPOSITORY "https://github.com/lief-project/LIEF.git"
    GIT_TAG 0.17.1
    UPDATE_DISCONNECTED ON
)

# LIEF compilation config
set(LIEF_USE_CCACHE     OFF CACHE INTERNAL "Do not use ccache")
set(LIEF_DOC            OFF CACHE INTERNAL "Do not generate lief documentation")
set(LIEF_PYTHON_API     OFF CACHE INTERNAL "Do not include python api")
set(LIEF_EXAMPLES       OFF CACHE INTERNAL "Do not build examples")
set(LIEF_TESTS          OFF CACHE INTERNAL "Do not run tests")
set(LIEF_C_API          OFF CACHE INTERNAL "Do not include C api")
set(LIEF_ENABLE_JSON    OFF CACHE INTERNAL "Do not include json api")
set(LIEF_DEX            OFF CACHE INTERNAL "Do not include support for DEX executable format")
set(LIEF_ART            OFF CACHE INTERNAL "Do not include support for ART executable format")
set(LIEF_OAT            OFF CACHE INTERNAL "Do not include support for OAT executable format")
set(LIEF_VDEX           OFF CACHE INTERNAL "Do not include support for VDEX executable format")

if(MSVC)
    set(LIEF_USE_CRT_DEBUG MDd CACHE INTERNAL "Set the correct build configuration")
    set(LIEF_USE_CRT_RELEASE MT CACHE INTERNAL "Set the correct build configuration")
    set(LIEF_USE_CRT_MINSIZEREL MT CACHE INTERNAL "Set the correct build configuration")
    set(LIEF_USE_CRT_RELWITHDEBINFO MTd CACHE INTERNAL "Set the correct build configuration")
endif()

# Path to where LIEF downloads third-party dependencies
set(LIEF_THIRD_PARTY_DIR "${CMAKE_BINARY_DIR}/_deps/lief-src/third-party")

FetchContent_MakeAvailable(lief)