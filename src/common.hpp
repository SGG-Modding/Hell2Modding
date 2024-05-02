#ifndef COMMON_INC
#define COMMON_INC

// clang-format off

#include <sdkddkver.h>
#include <winsock2.h>
#include <windows.h>

#include <cinttypes>
#include <cstddef>
#include <cstdint>

#include <chrono>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

#include <atomic>
#include <mutex>
#include <thread>

#include <memory>
#include <new>

#include <sstream>
#include <string>
#include <string_view>

#include <algorithm>
#include <functional>
#include <utility>

#include <set>
#include <stack>
#include <vector>

#include <typeinfo>
#include <type_traits>

#include <exception>
#include <stdexcept>

#include <any>
#include <optional>
#include <variant>

#include <format>
#include <nlohmann/json.hpp>

#include "logger/logger.hpp"
#include "lua/sol_include.hpp"
#include "gui/imgui_include.hpp"
#include "rom/rom.hpp"

// clang-format on

namespace big
{
	using namespace std::chrono_literals;

	inline HMODULE g_hmodule{};

	inline HANDLE g_main_thread{};
	inline DWORD g_main_thread_id{};

	inline std::atomic_bool g_abort{false};
	inline std::atomic_bool g_running{false};
} // namespace big

#endif
