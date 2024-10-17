include(FetchContent)

FetchContent_Declare(
	raw_pdb
	GIT_REPOSITORY https://github.com/MolecularMatters/raw_pdb.git
	GIT_TAG 479cd28468481f4df43cb392953da7e6636c70b6
)
FetchContent_MakeAvailable(raw_pdb)
