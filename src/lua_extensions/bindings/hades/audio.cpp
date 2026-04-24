#include "audio.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua_extensions/bindings/tolk/tolk.hpp>
#include <memory/byte_patch.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::audio
{
	// Bypasses the AudioData::Data.mActors membership check in LoadVoiceBank,
	// allowing voice banks with any name to be loaded (not just registered actors).
	// The check is purely a validation gate - no data from the found mActors entry
	// is used after the check, and invalid bank names fail gracefully at the FMOD
	// file-loading stage (FMOD_ERR_FILE_NOTFOUND is silently ignored).
	static void patch_LoadVoiceBank_actor_check()
	{
		static auto LoadVoiceBank_ptr = big::hades2_symbol_to_address["sgg::AudioManager::LoadVoiceBank"];
		if (!LoadVoiceBank_ptr)
		{
			LOG(WARNING) << "sgg::AudioManager::LoadVoiceBank not found in PDB; custom voice banks disabled";
			return;
		}

		// The mActors search loop ends with:
		//   cmp rbx, rdi          ; puVar27 != mActors end?
		//   je  <exit>            ; skip loading if actor not found (6-byte je near: 0F 84 xx xx xx xx)
		//   call getUSec          ; loading path starts here
		//
		// We scan for the distinctive loop tail: add rbx,0x30 / cmp rbx,rdi
		// The je gate is 9 bytes after the add instruction.
		//
		// Pattern: 48 83 C3 30  (add rbx, 0x30)
		//          48 3B DF     (cmp rbx, rdi)
		//          75 ??        (jne loop_top)
		//          48 3B DF     (cmp rbx, rdi)
		//          0F 84        (je near - the gate)
		auto func_base = LoadVoiceBank_ptr.as<uint8_t*>();

		// Scan within the first 0x300 bytes of the function for the mActors loop tail
		uint8_t* gate_addr = nullptr;
		for (size_t i = 0; i + 16 < 0x300; i++)
		{
			// Match: add rbx, 0x30 / cmp rbx, rdi / jne ?? / cmp rbx, rdi / je near
			if (func_base[i]     == 0x48 && func_base[i + 1] == 0x83 &&
			    func_base[i + 2] == 0xC3 && func_base[i + 3] == 0x30 && // add rbx, 0x30
			    func_base[i + 4] == 0x48 && func_base[i + 5] == 0x3B &&
			    func_base[i + 6] == 0xDF &&                              // cmp rbx, rdi
			    func_base[i + 7] == 0x75 &&                              // jne (short)
			    func_base[i + 9] == 0x48 && func_base[i + 10] == 0x3B &&
			    func_base[i + 11] == 0xDF &&                             // cmp rbx, rdi
			    func_base[i + 12] == 0x0F && func_base[i + 13] == 0x84)  // je (near)
			{
				gate_addr = func_base + i + 12;
				break;
			}
		}

		if (!gate_addr)
		{
			LOG(WARNING) << "LoadVoiceBank actor-check branch not found; custom voice banks disabled";
			return;
		}

		// NOP the 6-byte conditional jump (je near: 0F 84 xx xx xx xx)
		static constexpr uint8_t nops[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
		static_assert(sizeof(nops) == 6);
		for (size_t i = 0; i < sizeof(nops); i++)
		{
			memory::byte_patch::make(gate_addr + i, nops[i])->apply();
		}

		LOG(INFO) << "Patched LoadVoiceBank actor check - custom voice banks enabled";
	}

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

	void init()
	{
		patch_LoadVoiceBank_actor_check();
	}

	void bind(sol::table& state)
	{
		auto ns = state.create_named("audio");
		ns.set_function("load_bank", load_bank);
	}
} // namespace lua::hades::audio
