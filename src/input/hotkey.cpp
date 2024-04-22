#include "hotkey.hpp"

namespace big
{
	static std::map<std::string, uint8_t> VK_KEYS{
	    {"VK_LBUTTON", VK_LBUTTON},
	    {"VK_RBUTTON", VK_RBUTTON},
	    {"VK_CANCEL", VK_CANCEL},
	    {"VK_MBUTTON", VK_MBUTTON},
	    {"VK_XBUTTON1", VK_XBUTTON1},
	    {"VK_XBUTTON2", VK_XBUTTON2},
	    {"VK_BACK", VK_BACK},
	    {"VK_TAB", VK_TAB},
	    {"VK_CLEAR", VK_CLEAR},
	    {"VK_RETURN", VK_RETURN},
	    {"VK_SHIFT", VK_SHIFT},
	    {"VK_CONTROL", VK_CONTROL},
	    {"VK_MENU", VK_MENU},
	    {"VK_PAUSE", VK_PAUSE},
	    {"VK_CAPITAL", VK_CAPITAL},
	    {"VK_KANA", VK_KANA},
	    {"VK_HANGUL", VK_HANGUL},
	    {"VK_IME_ON", VK_IME_ON},
	    {"VK_JUNJA", VK_JUNJA},
	    {"VK_FINAL", VK_FINAL},
	    {"VK_HANJA", VK_HANJA},
	    {"VK_KANJI", VK_KANJI},
	    {"VK_IME_OFF", VK_IME_OFF},
	    {"VK_ESCAPE", VK_ESCAPE},
	    {"VK_CONVERT", VK_CONVERT},
	    {"VK_NONCONVERT", VK_NONCONVERT},
	    {"VK_ACCEPT", VK_ACCEPT},
	    {"VK_MODECHANGE", VK_MODECHANGE},
	    {"VK_SPACE", VK_SPACE},
	    {"VK_PRIOR", VK_PRIOR},
	    {"VK_NEXT", VK_NEXT},
	    {"VK_END", VK_END},
	    {"VK_HOME", VK_HOME},
	    {"VK_LEFT", VK_LEFT},
	    {"VK_UP", VK_UP},
	    {"VK_RIGHT", VK_RIGHT},
	    {"VK_DOWN", VK_DOWN},
	    {"VK_SELECT", VK_SELECT},
	    {"VK_PRINT", VK_PRINT},
	    {"VK_EXECUTE", VK_EXECUTE},
	    {"VK_SNAPSHOT", VK_SNAPSHOT},
	    {"VK_INSERT", VK_INSERT},
	    {"VK_DELETE", VK_DELETE},
	    {"VK_HELP", VK_HELP},
	    {"0", '0'},
	    {"1", '1'},
	    {"2", '2'},
	    {"3", '3'},
	    {"4", '4'},
	    {"5", '5'},
	    {"6", '6'},
	    {"7", '7'},
	    {"8", '8'},
	    {"9", '9'},
	    {"A", 'A'},
	    {"B", 'B'},
	    {"C", 'C'},
	    {"D", 'D'},
	    {"E", 'E'},
	    {"F", 'F'},
	    {"G", 'G'},
	    {"H", 'H'},
	    {"I", 'I'},
	    {"J", 'J'},
	    {"K", 'K'},
	    {"L", 'L'},
	    {"M", 'M'},
	    {"N", 'N'},
	    {"O", 'O'},
	    {"P", 'P'},
	    {"Q", 'Q'},
	    {"R", 'R'},
	    {"S", 'S'},
	    {"T", 'T'},
	    {"U", 'U'},
	    {"V", 'V'},
	    {"W", 'W'},
	    {"X", 'X'},
	    {"Y", 'Y'},
	    {"Z", 'Z'},
	    {"VK_LWIN", VK_LWIN},
	    {"VK_RWIN", VK_RWIN},
	    {"VK_APPS", VK_APPS},
	    {"VK_SLEEP", VK_SLEEP},
	    {"VK_NUMPAD0", VK_NUMPAD0},
	    {"VK_NUMPAD1", VK_NUMPAD1},
	    {"VK_NUMPAD2", VK_NUMPAD2},
	    {"VK_NUMPAD3", VK_NUMPAD3},
	    {"VK_NUMPAD4", VK_NUMPAD4},
	    {"VK_NUMPAD5", VK_NUMPAD5},
	    {"VK_NUMPAD6", VK_NUMPAD6},
	    {"VK_NUMPAD7", VK_NUMPAD7},
	    {"VK_NUMPAD8", VK_NUMPAD8},
	    {"VK_NUMPAD9", VK_NUMPAD9},
	    {"VK_MULTIPLY", VK_MULTIPLY},
	    {"VK_ADD", VK_ADD},
	    {"VK_SEPARATOR", VK_SEPARATOR},
	    {"VK_SUBTRACT", VK_SUBTRACT},
	    {"VK_DECIMAL", VK_DECIMAL},
	    {"VK_DIVIDE", VK_DIVIDE},
	    {"VK_F1", VK_F1},
	    {"VK_F2", VK_F2},
	    {"VK_F3", VK_F3},
	    {"VK_F4", VK_F4},
	    {"VK_F5", VK_F5},
	    {"VK_F6", VK_F6},
	    {"VK_F7", VK_F7},
	    {"VK_F8", VK_F8},
	    {"VK_F9", VK_F9},
	    {"VK_F10", VK_F10},
	    {"VK_F11", VK_F11},
	    {"VK_F12", VK_F12},
	    {"VK_F13", VK_F13},
	    {"VK_F14", VK_F14},
	    {"VK_F15", VK_F15},
	    {"VK_F16", VK_F16},
	    {"VK_F17", VK_F17},
	    {"VK_F18", VK_F18},
	    {"VK_F19", VK_F19},
	    {"VK_F20", VK_F20},
	    {"VK_F21", VK_F21},
	    {"VK_F22", VK_F22},
	    {"VK_F23", VK_F23},
	    {"VK_F24", VK_F24},
	    {"VK_NUMLOCK", VK_NUMLOCK},
	    {"VK_SCROLL", VK_SCROLL},
	    {"VK_LSHIFT", VK_LSHIFT},
	    {"VK_RSHIFT", VK_RSHIFT},
	    {"VK_LCONTROL", VK_LCONTROL},
	    {"VK_RCONTROL", VK_RCONTROL},
	    {"VK_LMENU", VK_LMENU},
	    {"VK_RMENU", VK_RMENU},
	    {"VK_BROWSER_BACK", VK_BROWSER_BACK},
	    {"VK_BROWSER_FORWARD", VK_BROWSER_FORWARD},
	    {"VK_BROWSER_REFRESH", VK_BROWSER_REFRESH},
	    {"VK_BROWSER_STOP", VK_BROWSER_STOP},
	    {"VK_BROWSER_SEARCH", VK_BROWSER_SEARCH},
	    {"VK_BROWSER_FAVORITES", VK_BROWSER_FAVORITES},
	    {"VK_BROWSER_HOME", VK_BROWSER_HOME},
	    {"VK_VOLUME_MUTE", VK_VOLUME_MUTE},
	    {"VK_VOLUME_DOWN", VK_VOLUME_DOWN},
	    {"VK_VOLUME_UP", VK_VOLUME_UP},
	    {"VK_MEDIA_NEXT_TRACK", VK_MEDIA_NEXT_TRACK},
	    {"VK_MEDIA_PREV_TRACK", VK_MEDIA_PREV_TRACK},
	    {"VK_MEDIA_STOP", VK_MEDIA_STOP},
	    {"VK_MEDIA_PLAY_PAUSE", VK_MEDIA_PLAY_PAUSE},
	    {"VK_LAUNCH_MAIL", VK_LAUNCH_MAIL},
	    {"VK_LAUNCH_MEDIA_SELECT", VK_LAUNCH_MEDIA_SELECT},
	    {"VK_LAUNCH_APP1", VK_LAUNCH_APP1},
	    {"VK_LAUNCH_APP2", VK_LAUNCH_APP2},
	    {"VK_OEM_1", VK_OEM_1},
	    {"VK_OEM_PLUS", VK_OEM_PLUS},
	    {"VK_OEM_COMMA", VK_OEM_COMMA},
	    {"VK_OEM_MINUS", VK_OEM_MINUS},
	    {"VK_OEM_PERIOD", VK_OEM_PERIOD},
	    {"VK_OEM_2", VK_OEM_2},
	    {"VK_OEM_3", VK_OEM_3},
	    {"VK_OEM_4", VK_OEM_4},
	    {"VK_OEM_5", VK_OEM_5},
	    {"VK_OEM_6", VK_OEM_6},
	    {"VK_OEM_7", VK_OEM_7},
	    {"VK_OEM_8", VK_OEM_8},
	    {"VK_OEM_102", VK_OEM_102},
	    {"VK_PROCESSKEY", VK_PROCESSKEY},
	    {"VK_PACKET", VK_PACKET},
	    {"VK_ATTN", VK_ATTN},
	    {"VK_CRSEL", VK_CRSEL},
	    {"VK_EXSEL", VK_EXSEL},
	    {"VK_EREOF", VK_EREOF},
	    {"VK_PLAY", VK_PLAY},
	    {"VK_ZOOM", VK_ZOOM},
	    {"VK_NONAME", VK_NONAME},
	    {"VK_PA1", VK_PA1},
	    {"VK_OEM_CLEAR", VK_OEM_CLEAR},
	};

	static std::map<uint8_t, std::string> VK_KEYS_REVERSE{
	    {VK_LBUTTON, "VK_LBUTTON"},
	    {VK_RBUTTON, "VK_RBUTTON"},
	    {VK_CANCEL, "VK_CANCEL"},
	    {VK_MBUTTON, "VK_MBUTTON"},
	    {VK_XBUTTON1, "VK_XBUTTON1"},
	    {VK_XBUTTON2, "VK_XBUTTON2"},
	    {VK_BACK, "VK_BACK"},
	    {VK_TAB, "VK_TAB"},
	    {VK_CLEAR, "VK_CLEAR"},
	    {VK_RETURN, "VK_RETURN"},
	    {VK_SHIFT, "VK_SHIFT"},
	    {VK_CONTROL, "VK_CONTROL"},
	    {VK_MENU, "VK_MENU"},
	    {VK_PAUSE, "VK_PAUSE"},
	    {VK_CAPITAL, "VK_CAPITAL"},
	    {VK_KANA, "VK_KANA"},
	    {VK_HANGUL, "VK_HANGUL"},
	    {VK_IME_ON, "VK_IME_ON"},
	    {VK_JUNJA, "VK_JUNJA"},
	    {VK_FINAL, "VK_FINAL"},
	    {VK_HANJA, "VK_HANJA"},
	    {VK_KANJI, "VK_KANJI"},
	    {VK_IME_OFF, "VK_IME_OFF"},
	    {VK_ESCAPE, "VK_ESCAPE"},
	    {VK_CONVERT, "VK_CONVERT"},
	    {VK_NONCONVERT, "VK_NONCONVERT"},
	    {VK_ACCEPT, "VK_ACCEPT"},
	    {VK_MODECHANGE, "VK_MODECHANGE"},
	    {VK_SPACE, "VK_SPACE"},
	    {VK_PRIOR, "VK_PRIOR"},
	    {VK_NEXT, "VK_NEXT"},
	    {VK_END, "VK_END"},
	    {VK_HOME, "VK_HOME"},
	    {VK_LEFT, "VK_LEFT"},
	    {VK_UP, "VK_UP"},
	    {VK_RIGHT, "VK_RIGHT"},
	    {VK_DOWN, "VK_DOWN"},
	    {VK_SELECT, "VK_SELECT"},
	    {VK_PRINT, "VK_PRINT"},
	    {VK_EXECUTE, "VK_EXECUTE"},
	    {VK_SNAPSHOT, "VK_SNAPSHOT"},
	    {VK_INSERT, "VK_INSERT"},
	    {VK_DELETE, "VK_DELETE"},
	    {VK_HELP, "VK_HELP"},
	    {'0', "0"},
	    {'1', "1"},
	    {'2', "2"},
	    {'3', "3"},
	    {'4', "4"},
	    {'5', "5"},
	    {'6', "6"},
	    {'7', "7"},
	    {'8', "8"},
	    {'9', "9"},
	    {'A', "A"},
	    {'B', "B"},
	    {'C', "C"},
	    {'D', "D"},
	    {'E', "E"},
	    {'F', "F"},
	    {'G', "G"},
	    {'H', "H"},
	    {'I', "I"},
	    {'J', "J"},
	    {'K', "K"},
	    {'L', "L"},
	    {'M', "M"},
	    {'N', "N"},
	    {'O', "O"},
	    {'P', "P"},
	    {'Q', "Q"},
	    {'R', "R"},
	    {'S', "S"},
	    {'T', "T"},
	    {'U', "U"},
	    {'V', "V"},
	    {'W', "W"},
	    {'X', "X"},
	    {'Y', "Y"},
	    {'Z', "Z"},
	    {VK_LWIN, "VK_LWIN"},
	    {VK_RWIN, "VK_RWIN"},
	    {VK_APPS, "VK_APPS"},
	    {VK_SLEEP, "VK_SLEEP"},
	    {VK_NUMPAD0, "VK_NUMPAD0"},
	    {VK_NUMPAD1, "VK_NUMPAD1"},
	    {VK_NUMPAD2, "VK_NUMPAD2"},
	    {VK_NUMPAD3, "VK_NUMPAD3"},
	    {VK_NUMPAD4, "VK_NUMPAD4"},
	    {VK_NUMPAD5, "VK_NUMPAD5"},
	    {VK_NUMPAD6, "VK_NUMPAD6"},
	    {VK_NUMPAD7, "VK_NUMPAD7"},
	    {VK_NUMPAD8, "VK_NUMPAD8"},
	    {VK_NUMPAD9, "VK_NUMPAD9"},
	    {VK_MULTIPLY, "VK_MULTIPLY"},
	    {VK_ADD, "VK_ADD"},
	    {VK_SEPARATOR, "VK_SEPARATOR"},
	    {VK_SUBTRACT, "VK_SUBTRACT"},
	    {VK_DECIMAL, "VK_DECIMAL"},
	    {VK_DIVIDE, "VK_DIVIDE"},
	    {VK_F1, "VK_F1"},
	    {VK_F2, "VK_F2"},
	    {VK_F3, "VK_F3"},
	    {VK_F4, "VK_F4"},
	    {VK_F5, "VK_F5"},
	    {VK_F6, "VK_F6"},
	    {VK_F7, "VK_F7"},
	    {VK_F8, "VK_F8"},
	    {VK_F9, "VK_F9"},
	    {VK_F10, "VK_F10"},
	    {VK_F11, "VK_F11"},
	    {VK_F12, "VK_F12"},
	    {VK_F13, "VK_F13"},
	    {VK_F14, "VK_F14"},
	    {VK_F15, "VK_F15"},
	    {VK_F16, "VK_F16"},
	    {VK_F17, "VK_F17"},
	    {VK_F18, "VK_F18"},
	    {VK_F19, "VK_F19"},
	    {VK_F20, "VK_F20"},
	    {VK_F21, "VK_F21"},
	    {VK_F22, "VK_F22"},
	    {VK_F23, "VK_F23"},
	    {VK_F24, "VK_F24"},
	    {VK_NUMLOCK, "VK_NUMLOCK"},
	    {VK_SCROLL, "VK_SCROLL"},
	    {VK_LSHIFT, "VK_LSHIFT"},
	    {VK_RSHIFT, "VK_RSHIFT"},
	    {VK_LCONTROL, "VK_LCONTROL"},
	    {VK_RCONTROL, "VK_RCONTROL"},
	    {VK_LMENU, "VK_LMENU"},
	    {VK_RMENU, "VK_RMENU"},
	    {VK_BROWSER_BACK, "VK_BROWSER_BACK"},
	    {VK_BROWSER_FORWARD, "VK_BROWSER_FORWARD"},
	    {VK_BROWSER_REFRESH, "VK_BROWSER_REFRESH"},
	    {VK_BROWSER_STOP, "VK_BROWSER_STOP"},
	    {VK_BROWSER_SEARCH, "VK_BROWSER_SEARCH"},
	    {VK_BROWSER_FAVORITES, "VK_BROWSER_FAVORITES"},
	    {VK_BROWSER_HOME, "VK_BROWSER_HOME"},
	    {VK_VOLUME_MUTE, "VK_VOLUME_MUTE"},
	    {VK_VOLUME_DOWN, "VK_VOLUME_DOWN"},
	    {VK_VOLUME_UP, "VK_VOLUME_UP"},
	    {VK_MEDIA_NEXT_TRACK, "VK_MEDIA_NEXT_TRACK"},
	    {VK_MEDIA_PREV_TRACK, "VK_MEDIA_PREV_TRACK"},
	    {VK_MEDIA_STOP, "VK_MEDIA_STOP"},
	    {VK_MEDIA_PLAY_PAUSE, "VK_MEDIA_PLAY_PAUSE"},
	    {VK_LAUNCH_MAIL, "VK_LAUNCH_MAIL"},
	    {VK_LAUNCH_MEDIA_SELECT, "VK_LAUNCH_MEDIA_SELECT"},
	    {VK_LAUNCH_APP1, "VK_LAUNCH_APP1"},
	    {VK_LAUNCH_APP2, "VK_LAUNCH_APP2"},
	    {VK_OEM_1, "VK_OEM_1"},
	    {VK_OEM_PLUS, "VK_OEM_PLUS"},
	    {VK_OEM_COMMA, "VK_OEM_COMMA"},
	    {VK_OEM_MINUS, "VK_OEM_MINUS"},
	    {VK_OEM_PERIOD, "VK_OEM_PERIOD"},
	    {VK_OEM_2, "VK_OEM_2"},
	    {VK_OEM_3, "VK_OEM_3"},
	    {VK_OEM_4, "VK_OEM_4"},
	    {VK_OEM_5, "VK_OEM_5"},
	    {VK_OEM_6, "VK_OEM_6"},
	    {VK_OEM_7, "VK_OEM_7"},
	    {VK_OEM_8, "VK_OEM_8"},
	    {VK_OEM_102, "VK_OEM_102"},
	    {VK_PROCESSKEY, "VK_PROCESSKEY"},
	    {VK_PACKET, "VK_PACKET"},
	    {VK_ATTN, "VK_ATTN"},
	    {VK_CRSEL, "VK_CRSEL"},
	    {VK_EXSEL, "VK_EXSEL"},
	    {VK_EREOF, "VK_EREOF"},
	    {VK_PLAY, "VK_PLAY"},
	    {VK_ZOOM, "VK_ZOOM"},
	    {VK_NONAME, "VK_NONAME"},
	    {VK_PA1, "VK_PA1"},
	    {VK_OEM_CLEAR, "VK_OEM_CLEAR"},
	};

	hotkey::hotkey(const std::string& name, uint8_t default_vk)
	{
		if (!name.size())
		{
			LOG(FATAL) << "Invalid hotkey";
			*(int*)0xDE'AD = 0;
		}
		m_name       = name;
		m_default_vk = default_vk;

		hotkeys.insert({m_name, this});
	}

	hotkey::~hotkey()
	{
		hotkeys.erase(m_name);
	}

	const uint8_t hotkey::get_vk_value() const
	{
		return m_vk_value;
	}

	void hotkey::set_vk_value(uint8_t new_vk)
	{
		m_vk_value                      = new_vk;
		m_vk_string->ref<std::string>() = VK_KEYS_REVERSE[new_vk];
	}

	void hotkey::init_hotkeys()
	{
		try
		{
			m_file_path = g_file_manager.get_project_folder("config").get_path() / m_file_name;
			if (std::filesystem::exists(m_file_path))
			{
				m_table = toml::parse_file(m_file_path.c_str());
			}
			m_last_saved_table = m_table;

			// Fill entries based on the serialized disk data if needed
			for (auto& hotkey_entry_pair : hotkeys)
			{
				const auto& hotkey_entry = hotkey_entry_pair.second;

				const auto hotkey_entry_doesnt_exist = !m_table.contains(hotkey_entry->m_name);
				if (hotkey_entry_doesnt_exist)
				{
					m_table.insert_or_assign(hotkey_entry->m_name, VK_KEYS_REVERSE[hotkey_entry->m_default_vk]);
				}

				hotkey_entry->m_vk_string = m_table.get(hotkey_entry->m_name);
				if (hotkey_entry->m_vk_string == nullptr)
				{
					LOG(FATAL) << "what";
				}
				else
				{
					if (hotkey_entry->m_vk_string->is_string())
					{
						if (VK_KEYS.contains(hotkey_entry->m_vk_string->ref<std::string>()))
						{
							hotkey_entry->m_vk_value = VK_KEYS[hotkey_entry->m_vk_string->ref<std::string>()];
						}
					}
					else if (hotkey_entry->m_vk_string->type() == toml::node_type::integer)
					{
						const auto vk_value = hotkey_entry->m_vk_string->ref<int64_t>();
						if (VK_KEYS_REVERSE.contains(vk_value))
						{
							hotkey_entry->m_vk_value = vk_value;
							m_table.insert_or_assign(hotkey_entry->m_name, VK_KEYS_REVERSE[vk_value]);
							hotkey_entry->m_vk_string = m_table.get(hotkey_entry->m_name);
						}
					}
					else
					{
						m_table.insert_or_assign(hotkey_entry->m_name, VK_KEYS_REVERSE[hotkey_entry->m_default_vk]);
						hotkey_entry->m_vk_string = m_table.get(hotkey_entry->m_name);
						hotkey_entry->m_vk_value  = VK_KEYS[hotkey_entry->m_vk_string->ref<std::string>()];
					}
				}

				if (hotkey_entry->m_vk_string == nullptr || hotkey_entry->m_vk_string->type() != toml::node_type::string
				    || !VK_KEYS.contains(hotkey_entry->m_vk_string->ref<std::string>()))
				{
					LOG(WARNING) << "Invalid serialized data. Clearing " << hotkey_entry->m_name << " hotkey";

					m_table.insert_or_assign(hotkey_entry->m_name, VK_KEYS_REVERSE[hotkey_entry->m_default_vk]);
					hotkey_entry->m_vk_string = m_table.get(hotkey_entry->m_name);
					if (hotkey_entry->m_vk_string == nullptr)
					{
						LOG(FATAL) << "what2";
					}
					else
					{
						if (hotkey_entry->m_vk_string->is_string())
						{
							hotkey_entry->m_vk_value = VK_KEYS[hotkey_entry->m_vk_string->ref<std::string>()];
						}
					}
				}
			}

			save_hotkeys_if_needed();
		}
		catch (const std::exception& e)
		{
			LOG(INFO) << "Failed init hotkeys: " << e.what();
		}
	}

	void hotkey::save_hotkeys_if_needed()
	{
		if (m_table != m_last_saved_table)
		{
			LOG(INFO) << "table was different, saving";
			save_hotkeys();
		}
	}

	void hotkey::save_hotkeys()
	{
		std::ofstream file_stream(m_file_path, std::ios::out | std::ios::trunc);
		if (file_stream.is_open())
		{
			file_stream << m_table;

			m_last_saved_table = m_table;
		}
		else
		{
			LOG(WARNING) << "Failed to save hotkeys.";
		}
	}

} // namespace big
