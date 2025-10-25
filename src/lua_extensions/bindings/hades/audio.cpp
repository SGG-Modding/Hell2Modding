#include "audio.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::audio
{
	static std::string g_fixed_path_fsAppendPathComponent;

	static void hook_fsAppendPathComponent(const char* basePath, const char* pathComponent, char* output /*size: 512*/)
	{
		// TODO: Add thread safety to g_fixed_path_fsAppendPathComponent, as fsAppendPathComponent is called from multiple threads.

		if (g_fixed_path_fsAppendPathComponent.size())
		{
			if (pathComponent && strstr(g_fixed_path_fsAppendPathComponent.c_str(), pathComponent))
			{
				LOG(INFO) << "setting path to " << g_fixed_path_fsAppendPathComponent.c_str() << " basePath: " << (basePath ? basePath :
				"<None>") << " | pathComponent: " << (pathComponent ? pathComponent : "<None>");
				strcpy(output, g_fixed_path_fsAppendPathComponent.c_str());
			}
		}
		else
		{
			big::g_hooking->get_original<hook_fsAppendPathComponent>()(basePath, pathComponent, output);
		}
	}

	// Lua API: Function
	// Table: audio
	// Name: load_bank
	// Param: file_path: string: Path to the fmod .bank to load
	// Returns: bool: Returns true if bank loaded successfully.
	// The game currently use FMod Studio `2.02.23`. You can query the version by clicking checking Properties -> Details of the game `fmodstudio.dll`.
	// If your sound events correcty play but nothing can be heard, make sure that the guid of the Mixer masterBus, MixerInput output and MixerMaster id matches one from the game, one known to work is the guid that can be found inside the vanilla game file GUIDS.txt, called bus:/Game
	// You'll want to string replace the guids in the (at minimum 2) .xml files Master, Mixer, and any Metadata/Event events files that were made before the guid setup change
	static bool load_bank(const std::string& file_path, sol::this_environment env)
	{
		enum class sgg__PackageGroup : uint8_t
		{
			Base = 2
		};

		static auto LoadBank_ptr = big::hades2_symbol_to_address["sgg::AudioManager::LoadBank"];
		if (LoadBank_ptr)
		{
			static auto LoadBank = LoadBank_ptr.as_func<void(eastl::string_view*, sgg__PackageGroup)>();

			static auto fsAppendPathComponent_ptr = big::hades2_symbol_to_address["fsAppendPathComponent"];
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.as_func<void(const char*, const char*, char*)>();

				static auto hook_once =
				    big::hooking::detour_hook_helper::add<hook_fsAppendPathComponent>("hook_fsAppendPathComponent", fsAppendPathComponent);

				std::string bank_name = (char*)std::filesystem::path(file_path).stem().u8string().c_str();
				eastl::string_view fp(bank_name.c_str(), bank_name.size());

				g_fixed_path_fsAppendPathComponent = file_path;
				LoadBank(&fp, sgg__PackageGroup::Base);
				g_fixed_path_fsAppendPathComponent = "";

				return true;
			}
		}

		return false;
	}

	void bind(sol::table& state)
	{
		auto ns = state.create_named("audio");
		ns.set_function("load_bank", load_bank);
	}
} // namespace lua::hades::audio
