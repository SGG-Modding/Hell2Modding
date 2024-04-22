#ifndef COMMON_INC
#define COMMON_INC

// clang-format off

#include <sdkddkver.h>
#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>

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

#define SOL_ALL_SAFETIES_ON 1
#define SOL_NO_CHECK_NUMBER_PRECISION 1
#include "sol/sol.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts
#include <imgui.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#define IM_ASSERT(_EXPR)  ((void)(_EXPR))     // Disable asserts

#include "logger/logger.hpp"

// clang-format on

namespace big
{
	using namespace std::chrono_literals;

	inline HMODULE g_hmodule{};

	inline HANDLE g_main_thread{};
	inline DWORD g_main_thread_id{};

	inline std::atomic_bool g_abort{false};
	inline std::atomic_bool g_running{false};

	inline constexpr auto g_project_name             = "Hell2Modding";
	inline constexpr auto g_target_window_class_name = "The Forge";
} // namespace big

#endif
