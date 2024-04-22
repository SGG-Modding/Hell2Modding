#pragma once

#include <toml.hpp>

namespace big
{
	class hotkey
	{
		std::string m_name;
		toml::node* m_vk_string = nullptr;
		uint8_t m_vk_value;
		uint8_t m_default_vk;

	public:
		hotkey() = delete;
		hotkey(const std::string& name, uint8_t default_vk);

		~hotkey();

		const uint8_t get_vk_value() const;
		void set_vk_value(uint8_t new_vk);

		static inline std::unordered_map<std::string, hotkey*> hotkeys;
		static inline constexpr auto m_file_name = "Hell2Modding-Hell2Modding-Hotkeys.cfg";
		static inline std::filesystem::path m_file_path;
		static inline toml::table m_table;
		static inline toml::table m_last_saved_table;
		static void init_hotkeys();
		static void save_hotkeys_if_needed();
		static void save_hotkeys();
	};
} // namespace big
