#pragma once
#include "thunderstore/v1/manifest.hpp"

namespace big
{
	struct module_info
	{
		std::string m_lua_file_entries_hash{};
		std::filesystem::path m_path{};
		std::filesystem::path m_folder_path{};
		std::string m_guid{};
		std::string m_guid_with_version{};
		ts::v1::manifest m_manifest{};
	};
} // namespace big
