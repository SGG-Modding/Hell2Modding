#include "audio.hpp"

#include <hades2/pdb_symbol_map.hpp>
#include <hooks/hooking.hpp>
#include <memory/gm_address.hpp>
#include <string/string.hpp>

namespace lua::hades::gpk
{
	struct gpk_pack_entry
	{
		std::string name;
		std::vector<char> decompressed;
	};

	std::vector<gpk_pack_entry> load_gpk_file(const std::filesystem::path &path)
	{
		if (path.extension() != ".gpk")
		{
			LOG(WARNING) << "Skipping non-.gpk file: " << path << "\n";
			return {};
		}

		static auto LZ4_decompress_safe = big::hades2_symbol_to_address["LZ4_decompress_safe"].as_func<__int64(const char *, char *, int, int)>();

		std::vector<gpk_pack_entry> entries;

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			LOG(ERROR) << "Failed to open file: " << path;
			return entries;
		}

		int version = 0;
		int count   = 0;

		file.read(reinterpret_cast<char *>(&version), sizeof(version));
		file.read(reinterpret_cast<char *>(&count), sizeof(count));

		//if (version != 1 || count < 0)
		//{
		//	LOG(ERROR) << "Invalid GPK header in " << path;
		//	return entries;
		//}

		for (int i = 0; i < count; ++i)
		{
			uint8_t nameLength = 0;
			file.read(reinterpret_cast<char *>(&nameLength), 1);
			if (!file || nameLength == 0)
			{
				LOG(ERROR) << "Invalid entry name length in " << path;
				break;
			}

			std::string entryName(nameLength, '\0');
			file.read(entryName.data(), nameLength);

			int fileSize = 0;
			file.read(reinterpret_cast<char *>(&fileSize), sizeof(fileSize));
			if (fileSize <= 0 || fileSize > 0x1'1F'FF'FF)
			{
				LOG(ERROR) << "Invalid entry size in " << path << " (" << entryName << ")\n";
				break;
			}

			std::vector<char> compressed(fileSize);
			file.read(compressed.data(), fileSize);
			if (file.gcount() != fileSize)
			{
				LOG(ERROR) << "Failed to read compressed data in " << path << " (" << entryName << ")\n";
				break;
			}

			// Allocate decompression buffer (fixed 0x1200000 = 18 MB like original code)
			constexpr int kMaxDecompressedSize = 0x1'20'00'00;
			std::vector<char> decompressed(kMaxDecompressedSize);

			int decompressedSize = LZ4_decompress_safe(compressed.data(), decompressed.data(), fileSize, kMaxDecompressedSize);

			if (decompressedSize < 0)
			{
				LOG(ERROR) << "LZ4 decompression failed for " << entryName;
				continue;
			}

			decompressed.resize(decompressedSize);
			entries.push_back({entryName, std::move(decompressed)});
		}

		return entries;
	}

	void put_gpk_entries_to_folder(const std::vector<gpk_pack_entry> &entries, const std::filesystem::path &output_folder)
	{
		if (!entries.size())
		{
			return;
		}

		if (!std::filesystem::exists(output_folder))
		{
			std::filesystem::create_directories(output_folder);
		}

		for (const auto &entry : entries)
		{
			std::filesystem::path outPath = output_folder / entry.name;
			if (outPath.extension() != ".gr2")
			{
				outPath += ".gr2";
			}

			// Ensure parent directory exists (in case names contain subdirs)
			if (outPath.has_parent_path())
			{
				std::filesystem::create_directories(outPath.parent_path());
			}

			std::ofstream outFile(outPath, std::ios::binary);
			if (!outFile)
			{
				LOG(ERROR) << "Failed to write file: " << outPath;
				continue;
			}

			outFile.write(entry.decompressed.data(), entry.decompressed.size());
			LOG(INFO) << "Wrote " << outPath << " (" << entry.decompressed.size() << " bytes)\n";
		}
	}

	// Lua API: Function
	// Table: gpk
	// Name: decompress_folder
	// Param: input_folder_path: string: Path to folder containing gpk compressed files.
	// Param: output_folder_path: string: Path to the folder where the decompressed files will be placed. The folder is created if needed.
	static void decompress_folder(const std::string &input_folder_path, const std::string &output_folder_path)
	{
		for (const auto &entry : std::filesystem::recursive_directory_iterator(input_folder_path, std::filesystem::directory_options::skip_permission_denied | std::filesystem::directory_options::follow_directory_symlink))
		{
			if (!entry.exists() || entry.is_directory())
			{
				continue;
			}

			put_gpk_entries_to_folder(load_gpk_file(entry.path()), output_folder_path / entry.path().stem());
		}
	}

	// Lua API: Function
	// Table: gpk
	// Name: decompress_file
	// Param: input_file_path: string: Path to a gpk file.
	// Param: output_folder_path: string: Path to the folder where the decompressed files will be placed. The folder is created if needed.
	static void decompress_file(const std::string &input_file_path, const std::string &output_folder_path)
	{
		if (!std::filesystem::exists(input_file_path))
		{
			LOG(ERROR) << input_file_path << " does not exist.";
			return;
		}

		put_gpk_entries_to_folder(load_gpk_file(input_file_path), output_folder_path);
	}

	void bind(sol::table &state)
	{
		auto ns = state.create_named("gpk");
		ns.set_function("decompress_folder", decompress_folder);
		ns.set_function("decompress_file", decompress_file);
	}
} // namespace lua::hades::gpk
