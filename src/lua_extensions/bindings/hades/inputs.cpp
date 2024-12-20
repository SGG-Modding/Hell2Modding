#include "inputs.hpp"

#include "lua_extensions/bindings/hades/hades_ida.hpp"
#include "lua_extensions/bindings/tolk/tolk.hpp"
#include "string/string.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <lua/lua_manager.hpp>
#include <lua_extensions/lua_module_ext.hpp>
#include <memory/gm_address.hpp>

namespace sgg
{
	enum KeyModifier : int32_t
	{
		Ctrl  = 1 << 0,
		Shift = 1 << 1,
		Alt   = 1 << 2,
	};

	enum KeyboardButtonId : int32_t
	{
		KeyNone             = -1,
		KeyEscape           = 0x0,
		KeyF1               = 0x1,
		KeyF2               = 0x2,
		KeyF3               = 0x3,
		KeyF4               = 0x4,
		KeyF5               = 0x5,
		KeyF6               = 0x6,
		KeyF7               = 0x7,
		KeyF8               = 0x8,
		KeyF9               = 0x9,
		KeyF10              = 0xA,
		KeyF11              = 0xB,
		KeyF12              = 0xC,
		KeyF13              = 0xD,
		KeyF14              = 0xE,
		KeyF15              = 0xF,
		KeyF16              = 0x10,
		KeyF17              = 0x11,
		KeyF18              = 0x12,
		KeyF19              = 0x13,
		KeyPrint            = 0x14,
		KeyScrollLock       = 0x15,
		KeyBreak            = 0x16,
		KeySpace            = 0x20,
		KeyApostrophe       = 0x27,
		KeyComma            = 0x2C,
		KeyMinus            = 0x2D,
		KeyPeriod           = 0x2E,
		KeySlash            = 0x2F,
		Key0                = 0x30,
		Key1                = 0x31,
		Key2                = 0x32,
		Key3                = 0x33,
		Key4                = 0x34,
		Key5                = 0x35,
		Key6                = 0x36,
		Key7                = 0x37,
		Key8                = 0x38,
		Key9                = 0x39,
		KeySemicolon        = 0x3B,
		KeyLess             = 0x3C,
		KeyEqual            = 0x3D,
		KeyA                = 0x41,
		KeyB                = 0x42,
		KeyC                = 0x43,
		KeyD                = 0x44,
		KeyE                = 0x45,
		KeyF                = 0x46,
		KeyG                = 0x47,
		KeyH                = 0x48,
		KeyI                = 0x49,
		KeyJ                = 0x4A,
		KeyK                = 0x4B,
		KeyL                = 0x4C,
		KeyM                = 0x4D,
		KeyN                = 0x4E,
		KeyO                = 0x4F,
		KeyP                = 0x50,
		KeyQ                = 0x51,
		KeyR                = 0x52,
		KeyS                = 0x53,
		KeyT                = 0x54,
		KeyU                = 0x55,
		KeyV                = 0x56,
		KeyW                = 0x57,
		KeyX                = 0x58,
		KeyY                = 0x59,
		KeyZ                = 0x5A,
		KeyBracketLeft      = 0x5B,
		KeyBackslash        = 0x5C,
		KeyBracketRight     = 0x5D,
		KeyGrave            = 0x60,
		KeyLeft             = 0x61,
		KeyRight            = 0x62,
		KeyUp               = 0x63,
		KeyDown             = 0x64,
		KeyInsert           = 0x65,
		KeyHome             = 0x66,
		KeyDelete           = 0x67,
		KeyEnd              = 0x68,
		KeyPageUp           = 0x69,
		KeyPageDown         = 0x6A,
		KeyNumLock          = 0x6B,
		KeyKpEqual          = 0x6C,
		KeyKpDivide         = 0x6D,
		KeyKpMultiply       = 0x6E,
		KeyKpSubtract       = 0x6F,
		KeyKpAdd            = 0x70,
		KeyKpEnter          = 0x71,
		KeyKpInsert         = 0x72,
		KeyKpEnd            = 0x73,
		KeyKpDown           = 0x74,
		KeyKpPageDown       = 0x75,
		KeyKpLeft           = 0x76,
		KeyKpBegin          = 0x77,
		KeyKpRight          = 0x78,
		KeyKpHome           = 0x79,
		KeyKpUp             = 0x7A,
		KeyKpPageUp         = 0x7B,
		KeyKpDelete         = 0x7C,
		KeyBackSpace        = 0x7D,
		KeyTab              = 0x7E,
		KeyReturn           = 0x7F,
		KeyCapsLock         = 0x80,
		KeyShiftL           = 0x81,
		KeyCtrlL            = 0x82,
		KeySuperL           = 0x83,
		KeyAltL             = 0x84,
		KeyAltR             = 0x85,
		KeySuperR           = 0x86,
		KeyMenu             = 0x87,
		KeyCtrlR            = 0x88,
		KeyShiftR           = 0x89,
		KeyBack             = 0x8A,
		KeySoftLeft         = 0x8B,
		KeySoftRight        = 0x8C,
		KeyCall             = 0x8D,
		KeyEndcall          = 0x8E,
		KeyStar             = 0x8F,
		KeyPound            = 0x90,
		KeyDpadCenter       = 0x91,
		KeyVolumeUp         = 0x92,
		KeyVolumeDown       = 0x93,
		KeyPower            = 0x94,
		KeyCamera           = 0x95,
		KeyClear            = 0x96,
		KeySymbol           = 0x97,
		KeyExplorer         = 0x98,
		KeyEnvelope         = 0x99,
		KeyEquals           = 0x9A,
		KeyAt               = 0x9B,
		KeyHeadsethook      = 0x9C,
		KeyFocus            = 0x9D,
		KeyPlus             = 0x9E,
		KeyNotification     = 0x9F,
		KeySearch           = 0xA0,
		KeyMediaPlayPause   = 0xA1,
		KeyMediaStop        = 0xA2,
		KeyMediaNext        = 0xA3,
		KeyMediaPrevious    = 0xA4,
		KeyMediaRewind      = 0xA5,
		KeyMediaFastForward = 0xA6,
		KeyMute             = 0xA7,
		KeyPictsymbols      = 0xA8,
		KeySwitchCharset    = 0xA9,
		KeyForward          = 0xAA,
		KeyExtra1           = 0xAB,
		KeyExtra2           = 0xAC,
		KeyExtra3           = 0xAD,
		KeyExtra4           = 0xAE,
		KeyExtra5           = 0xAF,
		KeyExtra6           = 0xB0,
		KeyFn               = 0xB1,
		KeyCircumflex       = 0xB2,
		KeySsharp           = 0xB3,
		KeyAcute            = 0xB4,
		KeyAltGr            = 0xB5,
		KeyNumbersign       = 0xB6,
		KeyUdiaeresis       = 0xB7,
		KeyAdiaeresis       = 0xB8,
		KeyOdiaeresis       = 0xB9,
		KeySection          = 0xBA,
		KeyAring            = 0xBB,
		KeyDiaeresis        = 0xBC,
		KeyTwosuperior      = 0xBD,
		KeyRightParenthesis = 0xBE,
		KeyDollar           = 0xBF,
		KeyUgrave           = 0xC0,
		KeyAsterisk         = 0xC1,
		KeyColon            = 0xC2,
		KeyExclam           = 0xC3,
		KeyBraceLeft        = 0xC4,
		KeyBraceRight       = 0xC5,
		KeySysRq            = 0xC6,
		KeyNumLck           = 0xC7,
		KeyCount_           = 0xC8,
	};

	// clang-format off
std::unordered_map<std::string, sgg::KeyboardButtonId> key_map = {
        { "Escape", sgg::KeyboardButtonId::KeyEscape },
        { "F1", sgg::KeyboardButtonId::KeyF1 },
        { "F2", sgg::KeyboardButtonId::KeyF2 },
        { "F3", sgg::KeyboardButtonId::KeyF3 },
        { "F4", sgg::KeyboardButtonId::KeyF4 },
        { "F5", sgg::KeyboardButtonId::KeyF5 },
        { "F6", sgg::KeyboardButtonId::KeyF6 },
        { "F7", sgg::KeyboardButtonId::KeyF7 },
        { "F8", sgg::KeyboardButtonId::KeyF8 },
        { "F9", sgg::KeyboardButtonId::KeyF9 },
        { "F10", sgg::KeyboardButtonId::KeyF10 },
        { "F11", sgg::KeyboardButtonId::KeyF11 },
        { "F12", sgg::KeyboardButtonId::KeyF12 },
        { "Space", sgg::KeyboardButtonId::KeySpace },
        { "OemQuotes", sgg::KeyboardButtonId::KeyApostrophe },
        { "OemComma", sgg::KeyboardButtonId::KeyComma },
        { "OemMinus", sgg::KeyboardButtonId::KeyKpSubtract },
        { "OemPeriod", sgg::KeyboardButtonId::KeyKpDelete },
        { "D0", sgg::KeyboardButtonId::Key0 },
        { "D1", sgg::KeyboardButtonId::Key1 },
        { "D2", sgg::KeyboardButtonId::Key2 },
        { "D3", sgg::KeyboardButtonId::Key3 },
        { "D4", sgg::KeyboardButtonId::Key4 },
        { "D5", sgg::KeyboardButtonId::Key5 },
        { "D6", sgg::KeyboardButtonId::Key6 },
        { "D7", sgg::KeyboardButtonId::Key7 },
        { "D8", sgg::KeyboardButtonId::Key8 },
        { "D9", sgg::KeyboardButtonId::Key9 },
        { "A", sgg::KeyboardButtonId::KeyA },
        { "B", sgg::KeyboardButtonId::KeyB },
        { "C", sgg::KeyboardButtonId::KeyC },
        { "D", sgg::KeyboardButtonId::KeyD },
        { "E", sgg::KeyboardButtonId::KeyE },
        { "F", sgg::KeyboardButtonId::KeyF },
        { "G", sgg::KeyboardButtonId::KeyG },
        { "H", sgg::KeyboardButtonId::KeyH },
        { "I", sgg::KeyboardButtonId::KeyI },
        { "J", sgg::KeyboardButtonId::KeyJ },
        { "K", sgg::KeyboardButtonId::KeyK },
        { "L", sgg::KeyboardButtonId::KeyL },
        { "M", sgg::KeyboardButtonId::KeyM },
        { "N", sgg::KeyboardButtonId::KeyN },
        { "O", sgg::KeyboardButtonId::KeyO },
        { "P", sgg::KeyboardButtonId::KeyP },
        { "Q", sgg::KeyboardButtonId::KeyQ },
        { "R", sgg::KeyboardButtonId::KeyR },
        { "S", sgg::KeyboardButtonId::KeyS },
        { "T", sgg::KeyboardButtonId::KeyT },
        { "U", sgg::KeyboardButtonId::KeyU },
        { "V", sgg::KeyboardButtonId::KeyV },
        { "W", sgg::KeyboardButtonId::KeyW },
        { "X", sgg::KeyboardButtonId::KeyX },
        { "Y", sgg::KeyboardButtonId::KeyY },
        { "Z", sgg::KeyboardButtonId::KeyZ },
        { "OemPipe", sgg::KeyboardButtonId::KeyBackslash },
        { "Left", sgg::KeyboardButtonId::KeyLeft },
        { "Right", sgg::KeyboardButtonId::KeyRight },
        { "Up", sgg::KeyboardButtonId::KeyUp },
        { "Down", sgg::KeyboardButtonId::KeyDown },
        { "Insert", sgg::KeyboardButtonId::KeyInsert },
        { "Home", sgg::KeyboardButtonId::KeyHome },
        { "Delete", sgg::KeyboardButtonId::KeyDelete },
        { "End", sgg::KeyboardButtonId::KeyEnd },
        { "PageUp", sgg::KeyboardButtonId::KeyPageUp },
        { "PageDown", sgg::KeyboardButtonId::KeyPageDown },
        { "Multiply", sgg::KeyboardButtonId::KeyKpMultiply },
        { "Add", sgg::KeyboardButtonId::KeyKpAdd },
        { "NumPad0", sgg::KeyboardButtonId::KeyKpInsert },
        { "NumPad1", sgg::KeyboardButtonId::KeyKpEnd },
        { "NumPad2", sgg::KeyboardButtonId::KeyKpDown },
        { "NumPad3", sgg::KeyboardButtonId::KeyKpPageDown },
        { "NumPad4", sgg::KeyboardButtonId::KeyKpLeft },
        { "NumPad5", sgg::KeyboardButtonId::KeyKpBegin },
        { "NumPad6", sgg::KeyboardButtonId::KeyKpRight },
        { "NumPad7", sgg::KeyboardButtonId::KeyKpHome },
        { "NumPad8", sgg::KeyboardButtonId::KeyKpUp },
        { "NumPad9", sgg::KeyboardButtonId::KeyKpPageUp },
        { "Back", sgg::KeyboardButtonId::KeyBackSpace },
        { "Tab", sgg::KeyboardButtonId::KeyTab },
        { "Enter", sgg::KeyboardButtonId::KeyReturn },
        { "CapsLock", sgg::KeyboardButtonId::KeyCapsLock },
        { "LeftShift", sgg::KeyboardButtonId::KeyShiftL },
        { "LeftControl", sgg::KeyboardButtonId::KeyCtrlL },
        { "LeftWindows", sgg::KeyboardButtonId::KeySuperL },
        { "LeftAlt", sgg::KeyboardButtonId::KeyAltL },
        { "RightAlt", sgg::KeyboardButtonId::KeyAltR },
        { "RightWindows", sgg::KeyboardButtonId::KeySuperR },
        { "RightControl", sgg::KeyboardButtonId::KeyCtrlR },
        { "RightShift", sgg::KeyboardButtonId::KeyShiftR },
        { "OemPlus", sgg::KeyboardButtonId::KeyPlus },
        { "OemOpenBrackets", sgg::KeyboardButtonId::KeyExtra1 },
        { "OemCloseBrackets", sgg::KeyboardButtonId::KeyExtra2 },
        { "OemQuestion", sgg::KeyboardButtonId::KeyExtra3 },
        { "OemTilde", sgg::KeyboardButtonId::KeyExtra5 },
        { "OemSemicolon", sgg::KeyboardButtonId::KeyExtra6 },
};

	// clang-format on
} // namespace sgg

namespace lua::hades::inputs
{
	bool enable_vanilla_debug_keybinds       = false;
	bool let_game_input_go_through_gui_layer = false;

	std::map<std::string, std::vector<keybind_callback>> vanilla_key_callbacks;
	static gmAddress RegisterDebugKey{};

	static void invoke_debug_key_callback(uintptr_t mCallback)
	{
		// offset to get mName from DebugAction
		auto *mName_ = (eastl::string *)(mCallback - 0x18);

		std::string mName = mName_->c_str();

		if (enable_vanilla_debug_keybinds)
		{
			const auto it_callback = vanilla_key_callbacks.find(mName);
			if (it_callback != vanilla_key_callbacks.end())
			{
				LOG(DEBUG) << it_callback->first << " (Vanilla)";

				for (auto &cb : it_callback->second)
				{
					LOG(DEBUG) << cb.name << " (Vanilla)";
					cb.cb();
				}
			}
		}

		std::scoped_lock guard(big::g_lua_manager->m_module_lock);
		for (const auto &mod_ : big::g_lua_manager->m_modules)
		{
			auto mod               = (big::lua_module_ext *)mod_.get();
			const auto it_callback = mod->m_data_ext.m_keybinds.find(mName);
			if (it_callback != mod->m_data_ext.m_keybinds.end())
			{
				LOG(DEBUG) << it_callback->first << " (" << mod->guid() << ")";

				for (auto &cb : it_callback->second)
				{
					LOG(DEBUG) << cb.name;
					cb.cb();
				}
			}
		}
	}

	static void parse_and_register_keybind(std::string &keybind, const sol::coroutine &callback, const std::string &name, auto &RegisterDebugKey, bool is_vanilla, big::lua_module_ext *mod)
	{
		eastl_custom::function<void(uintptr_t)> funcy;
		funcy.mMgrFuncPtr    = nullptr;
		funcy.mInvokeFuncPtr = invoke_debug_key_callback;
		eastl::string callback_name{keybind.data(), keybind.size()};
		eastl::string nothing{};
		eastl::string nothing2{};

		int32_t key_modifier = 0;
		int32_t key          = 0;
		if (keybind.size())
		{
			if (keybind.contains("Control") || keybind.contains("Ctrl"))
			{
				key_modifier |= sgg::KeyModifier::Ctrl;
			}
			if (keybind.contains("Shift"))
			{
				key_modifier |= sgg::KeyModifier::Shift;
			}
			if (keybind.contains("Alt"))
			{
				key_modifier |= sgg::KeyModifier::Alt;
			}

			if (!keybind.contains(" "))
			{
				keybind = std::string("None ").append(keybind);
			}

			std::string key_str = big::string::split(keybind, ' ')[1];
			auto it_key         = sgg::key_map.find(key_str);
			if (it_key != sgg::key_map.end())
			{
				key = it_key->second;

				if (is_vanilla)
				{
					LOG(DEBUG) << "Vanilla Keybind Registered: " << keybind << " - " << name;
					vanilla_key_callbacks[keybind].emplace_back(name, callback);
				}
				else if (mod)
				{
					LOG(INFO) << mod->guid() << " Keybind Registered: " << keybind << " - " << name;
					mod->m_data_ext.m_keybinds[keybind].emplace_back(name, callback);
				}
			}
		}

		RegisterDebugKey(key_modifier, key, &funcy, &callback_name, nullptr, nullptr, 0, &nothing, &nothing2, 0);
	}

	// Lua API: Function
	// Table: inputs
	// Name: on_key_pressed
	// Param: [1]: string: The key binding string representing the keys that, when pressed, will trigger the callback function. The format used is the one used by the vanilla game, please check the vanilla scripts using "OnKeyPressed".
	// Param: [2]: function: The function to be called when the specified keybind is pressed.
	// Param: Name: string: Optional. The name linked to this keybind, used in the GUI to help the user know what it corresponds to.
	// Returns: table: Returns a handle to use, in case you want to remove this specific keybind.
	// The parameters must be inside a table. Check how the vanilla game does it through `OnKeyPressed`.
	// For every possible keys, please refer to [this map](https://github.com/SGG-Modding/Hell2Modding/blob/6d1cb8ed8870a401ac1cefd599bf2ae3a270d949/src/lua_extensions/bindings/hades/inputs.cpp#L204-L298)
	//
	// **Example Usage:**
	// ```lua
	// local handle = inputs.on_key_pressed{"Ctrl X", Name = "Testing key 2", function()
	//     print("hello there")
	// end}
	// ```
	static sol::table on_key_pressed(sol::table args, sol::this_environment env)
	{
		sol::table res(env.env.value().lua_state(), sol::create);

		auto mod = (big::lua_module_ext *)big::lua_module::this_from(env);
		if (mod)
		{
			if (RegisterDebugKey)
			{
				auto keybind_opt       = args[1].get<std::optional<std::string>>();
				auto callback_opt      = args[2].get<std::optional<sol::coroutine>>();
				auto callback_name_opt = args["Name"].get<std::optional<std::string>>();

				if (keybind_opt.has_value() && keybind_opt->size() && callback_opt.has_value() && callback_opt->valid())
				{
					auto RegisterDebugKey_good_type = RegisterDebugKey.as_func<void(int32_t a1, int32_t, eastl_custom::function<void(uintptr_t)> *, eastl::string *, void *, void *, bool, eastl::string *, eastl::string *, bool)>();
					parse_and_register_keybind(*keybind_opt, *callback_opt, callback_name_opt ? *callback_name_opt : "", RegisterDebugKey_good_type, false, mod);

					res[1] = *keybind_opt;
					res[2] = mod->m_data_ext.m_keybinds[*keybind_opt].size() - 1;
				}
			}
		}

		return res;
	}

	// Lua API: Function
	// Table: inputs
	// Name: remove_on_key_pressed
	// Param: handle: table: The handle that was returned to you from the on_key_pressed call.
	// Remove a keybind previously added through `on_key_pressed`.
	static void remove_on_key_pressed(sol::table handle, sol::this_environment env)
	{
		const auto mod = (big::lua_module_ext *)big::lua_module::this_from(env);
		if (mod)
		{
			const auto keybind_opt = handle[1].get<std::optional<std::string>>();
			const auto index_opt   = handle[2].get<std::optional<size_t>>();
			if (keybind_opt.has_value() && index_opt.has_value())
			{
				auto &vec        = mod->m_data_ext.m_keybinds[keybind_opt.value()];
				const auto index = index_opt.value();
				if (index >= 0 && index < vec.size())
				{
					vec.erase(vec.begin() + index);
				}
			}
		}
	}

	void bind(sol::state_view &state, sol::table &lua_ext)
	{
		static auto RegisterDebugKey_ptr = big::hades2_symbol_to_address["sgg::Debug::RegisterDebugKey"];
		if (RegisterDebugKey_ptr)
		{
			RegisterDebugKey = RegisterDebugKey_ptr;
		}

		state["OnKeyPressed"] = [](sol::table args)
		{
			if (RegisterDebugKey)
			{
				auto keybind_opt       = args[1].get<std::optional<std::string>>();
				auto callback_opt      = args[2].get<std::optional<sol::coroutine>>();
				auto callback_name_opt = args["Name"].get<std::optional<std::string>>();
				auto is_safe_opt       = args["Safe"].get<std::optional<bool>>();

				if (keybind_opt.has_value() && keybind_opt->size() && callback_opt.has_value() && callback_opt->valid())
				{
					auto RegisterDebugKey_good_type = RegisterDebugKey.as_func<void(int32_t a1, int32_t, eastl_custom::function<void(uintptr_t)> *, eastl::string *, void *, void *, bool, eastl::string *, eastl::string *, bool)>();
					parse_and_register_keybind(*keybind_opt, *callback_opt, callback_name_opt ? *callback_name_opt : "", RegisterDebugKey_good_type, true, nullptr);
				}
			}
		};

		auto ns = lua_ext.create_named("inputs");

		ns.set_function("on_key_pressed", on_key_pressed);
		ns.set_function("remove_on_key_pressed", remove_on_key_pressed);

		// Lua API: Function
		// Table: inputs
		// Name: let_game_input_go_through_gui_layer
		// Param: new_value: bool: Optional. Set the backing field to the passed new value.
		// Allows game input to be processed even when the GUI layer is active. This is useful for scenarios where you need the game to remain responsive to player actions or on key presses callbacks despite overlay interfaces.
		ns["let_game_input_go_through_gui_layer"] = sol::overload(
		    []() -> bool
		    {
			    return let_game_input_go_through_gui_layer;
		    },
		    [](bool new_value) -> void
		    {
			    let_game_input_go_through_gui_layer = new_value;
		    });

		// Lua API: Function
		// Table: inputs
		// Name: enable_vanilla_debug_keybinds
		// Param: new_value: bool: Optional. Set the backing field to the passed new value.
		// Enables the default debug key bindings used in the vanilla game.
		ns["enable_vanilla_debug_keybinds"] = sol::overload(
		    []() -> bool
		    {
			    return enable_vanilla_debug_keybinds;
		    },
		    [](bool new_value) -> void
		    {
			    enable_vanilla_debug_keybinds = new_value;
		    });
	}
} // namespace lua::hades::inputs
