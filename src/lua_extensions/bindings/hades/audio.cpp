#include "audio.hpp"

#include <hooks/hooking.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::audio
{
	static bool need_path_fix_fsAppendPathComponent     = false;
	static std::string fixed_path_fsAppendPathComponent = "";

	static void hook_fsAppendPathComponent(const char* basePath, const char* pathComponent, char* output /*size: 512*/)
	{
		if (need_path_fix_fsAppendPathComponent)
		{
			LOG(INFO) << "setting path to " << fixed_path_fsAppendPathComponent;
			strcpy(output, fixed_path_fsAppendPathComponent.c_str());
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
	static bool load_bank(const std::string& file_path, sol::this_environment env)
	{
		enum class sgg__PackageGroup : uint8_t
		{
			Base = 2
		};

		static auto LoadBank_ptr = gmAddress::scan("90 84 C0 75 2B", "sgg::AudioManager::LoadBank");
		if (LoadBank_ptr)
		{
			static auto LoadBank = LoadBank_ptr.offset(-0x2B).as_func<void(eastl::string_view*, sgg__PackageGroup)>();

			static auto fsAppendPathComponent_ptr = gmAddress::scan("C6 44 24 30 5C", "fsAppendPathComponent");
			if (fsAppendPathComponent_ptr)
			{
				static auto fsAppendPathComponent = fsAppendPathComponent_ptr.offset(-0x97).as_func<void(const char*, const char*, char*)>();

				static auto hook_once =
				    big::hooking::detour_hook_helper::add<hook_fsAppendPathComponent>("hook_fsAppendPathComponent", fsAppendPathComponent);

				std::string bank_name = (char*)std::filesystem::path(file_path).stem().u8string().c_str();
				eastl::string_view fp(bank_name.c_str(), bank_name.size());

				need_path_fix_fsAppendPathComponent = true;
				fixed_path_fsAppendPathComponent    = file_path;
				LoadBank(&fp, sgg__PackageGroup::Base);
				need_path_fix_fsAppendPathComponent = false;
				fixed_path_fsAppendPathComponent    = "";

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
