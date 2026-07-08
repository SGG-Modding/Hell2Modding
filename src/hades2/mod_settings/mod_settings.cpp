#include "mod_settings.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <memory/gm_address.hpp>

// clang-format off
#include <AsyncLogger/Logger.hpp>
using namespace al;
// clang-format on
#undef ERROR

namespace big::mod_settings
{
	// The in-game options menu is the native C++ screen sgg::MiscSettingsScreen. Its
	// categories and per-category option widgets are built in the engine with no Lua
	// entry point, so the "Mods" category has to be added by hooking the screen itself.
	// The constructor builds the category buttons and their widgets, making it the point
	// at which the extra category is injected.
	//   ctor(this, sgg::ScreenManager*, sgg::MenuScreen* opened_from, eastl::string& profile_name)
	static void* hook_MiscSettingsScreen_ctor(void* self, void* screen_manager, void* opened_from, void* profile_name)
	{
		// The engine constructor returns `this`; forward it unchanged.
		auto* screen = big::g_hooking->get_original<hook_MiscSettingsScreen_ctor>()(self, screen_manager, opened_from, profile_name);

		// TODO: add the "Mods" category button and render its panel of mod settings.

		return screen;
	}

	void register_hooks()
	{
		const auto ctor = big::hades2_symbol_to_address["sgg::MiscSettingsScreen::MiscSettingsScreen"];
		if (!ctor)
		{
			LOG(WARNING)
			    << "sgg::MiscSettingsScreen::MiscSettingsScreen not found; the in-game Mods options tab is unavailable";
			return;
		}

		static auto hook_ = hooking::detour_hook_helper::add_queue<hook_MiscSettingsScreen_ctor>(
		    "sgg::MiscSettingsScreen::MiscSettingsScreen",
		    ctor);
	}
} // namespace big::mod_settings
