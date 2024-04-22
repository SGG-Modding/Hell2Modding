#pragma once

#include "paths.hpp"

namespace big::paths
{
	static LPSTR* CommandLineToArgvA(LPCSTR cmd_line, int* argc)
	{
		ULONG len = strlen(cmd_line);
		ULONG i   = ((len + 2) / 2) * sizeof(LPVOID) + sizeof(LPVOID);

		LPSTR* argv = (LPSTR*)GlobalAlloc(GMEM_FIXED, i + (len + 2) * sizeof(CHAR));

		LPSTR _argv = (LPSTR)(((PUCHAR)argv) + i);

		ULONG _argc   = 0;
		argv[_argc]   = _argv;
		BOOL in_qm    = FALSE;
		BOOL in_text  = FALSE;
		BOOL in_space = TRUE;
		ULONG j       = 0;
		i             = 0;

		CHAR a;
		while ((a = cmd_line[i]))
		{
			if (in_qm)
			{
				if (a == '\"')
				{
					in_qm = FALSE;
				}
				else
				{
					_argv[j] = a;
					j++;
				}
			}
			else
			{
				switch (a)
				{
				case '\"':
					in_qm   = TRUE;
					in_text = TRUE;
					if (in_space)
					{
						argv[_argc] = _argv + j;
						_argc++;
					}
					in_space = FALSE;
					break;
				case ' ':
				case '\t':
				case '\n':
				case '\r':
					if (in_text)
					{
						_argv[j] = '\0';
						j++;
					}
					in_text  = FALSE;
					in_space = TRUE;
					break;
				default:
					in_text = TRUE;
					if (in_space)
					{
						argv[_argc] = _argv + j;
						_argc++;
					}
					_argv[j] = a;
					j++;
					in_space = FALSE;
					break;
				}
			}
			i++;
		}
		_argv[j]    = '\0';
		argv[_argc] = NULL;

		(*argc) = _argc;
		return argv;
	}

	std::filesystem::path get_project_root_folder()
	{
		std::filesystem::path root_folder{};

		constexpr auto root_folder_arg_name = "return_of_modding_root_folder";

		const char* env_root_folder = std::getenv(root_folder_arg_name);
		if (env_root_folder)
		{
			root_folder  = env_root_folder;
			root_folder /= g_project_name;
			LOG(INFO) << "Root folder set through env variable: "
			          << reinterpret_cast<const char*>(root_folder.u8string().c_str());
			if (!std::filesystem::exists(root_folder))
			{
				std::filesystem::create_directories(root_folder);
			}
		}
		else
		{
			try
			{
				cxxopts::Options options(g_project_name);
				auto* args  = GetCommandLineA();
				int argc    = 0;
				auto** argv = CommandLineToArgvA(args, &argc);

				options.add_options()(root_folder_arg_name, root_folder_arg_name, cxxopts::value<std::string>()->default_value(""));

				const auto result = options.parse(argc, argv);

				if (result.count(root_folder_arg_name))
				{
					root_folder  = result[root_folder_arg_name].as<std::string>();
					root_folder /= g_project_name;
					LOG(INFO) << "Root folder set through command line args: "
					          << reinterpret_cast<const char*>(root_folder.u8string().c_str());
					if (!std::filesystem::exists(root_folder))
					{
						std::filesystem::create_directories(root_folder);
					}
				}

				LocalFree(argv);
			}
			catch (const std::exception& e)
			{
				LOG(WARNING) << "Failed parsing cmd line args " << e.what();
			}
		}

		if (root_folder.empty() || !std::filesystem::exists(root_folder))
		{
			char module_file_path[MAX_PATH];
			const auto path_size = GetModuleFileNameA(nullptr, module_file_path, MAX_PATH);
			root_folder          = std::string(module_file_path, path_size);
			root_folder          = root_folder.parent_path() / g_project_name;
			LOG(INFO) << "Root folder set through default (game folder): "
			          << reinterpret_cast<const char*>(root_folder.u8string().c_str());
			if (!std::filesystem::exists(root_folder))
			{
				std::filesystem::create_directories(root_folder);
			}
		}

		return root_folder;
	}

	static std::filesystem::path dump_file_path;

	void init_dump_file_path()
	{
		dump_file_path = g_file_manager.get_project_file("./Hell2Modding_crash.dmp").get_path();
	}

	const std::filesystem::path& remove_and_get_dump_file_path()
	{
		try
		{
			if (std::filesystem::exists(dump_file_path))
			{
				std::filesystem::remove(dump_file_path);
			}
		}
		catch (const std::exception& e)
		{
			LOG(FATAL) << e.what();
		}

		return dump_file_path;
	}
} // namespace big::paths
