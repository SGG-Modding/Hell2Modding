#include "audio.hpp"

#include <hooks/hooking.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::lz4
{
	// Lua API: Function
	// Table: lz4
	// Name: decompress_folder
	// Param: folder_path_with_lz4_compressed_files: string: Path to folder containing lz4 compressed files.
	// Param: output_folder_path: string: Path to the folder where decompressed files will be placed.
	static void decompress_folder(const std::string &folder_path_with_lz4_compressed_files, const std::string &output_folder_path)
	{
		static auto lz4_decompress_safe = gmAddress::scan("E9 B0 05 00 00", "lz4_decompress_safe").offset(-0x77).as_func<__int64(const char *, char *, int, int)>();

		for (const auto &entry : std::filesystem::recursive_directory_iterator(folder_path_with_lz4_compressed_files, std::filesystem::directory_options::skip_permission_denied))
		{
			if (!entry.exists())
			{
				continue;
			}
			std::ifstream compressed_file(entry.path(), std::ios::binary);
			compressed_file.seekg(0, std::ios::end);
			std::streampos fileSize = compressed_file.tellg();
			compressed_file.seekg(0, std::ios::beg);
			std::vector<uint8_t> compressed_buffer(fileSize);
			compressed_file.read((char *)compressed_buffer.data(), fileSize);
			compressed_file.close();

			std::vector<uint8_t> decompressed_buffer(20'971'520);
			const auto decompressed_size = lz4_decompress_safe((char *)compressed_buffer.data(),
			                                                   (char *)decompressed_buffer.data(),
			                                                   compressed_buffer.size(),
			                                                   20'971'520);

			std::filesystem::path dump_file_path  = output_folder_path;
			dump_file_path                       /= entry.path().stem();
			std::ofstream file(dump_file_path, std::ios::out | std::ios::binary);

			if (!file.is_open())
			{
				LOG(FATAL) << "Failed to open file " << (char *)dump_file_path.u8string().c_str();
			}

			file.write((char *)decompressed_buffer.data(), decompressed_size);

			if (!file)
			{
				LOG(FATAL) << "Failed to write to file";
			}

			file.close();
		}
	}

	void bind(sol::table &state)
	{
		auto ns = state.create_named("lz4");
		ns.set_function("decompress_folder", decompress_folder);
	}
} // namespace lua::hades::lz4
