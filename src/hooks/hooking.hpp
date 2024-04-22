#pragma once
#include "call_hook.hpp"
#include "detour_hook.hpp"
#include "vmt_hook.hpp"
#include "vtable_hook.hpp"

namespace big
{
	class hooking
	{
	public:
		explicit hooking();
		~hooking();

		void enable();
		void disable();

		class detour_hook_helper
		{
			friend hooking;

			using ret_ptr_fn = std::function<void*()>;

			ret_ptr_fn m_on_hooking_available = nullptr;

			detour_hook* m_detour_hook;

			void enable_now();
			void enable_hook_if_hooking_is_already_running();

			static inline std::unordered_map<void*, detour_hook> m_target_func_to_detour_hook;

			template<auto detour_function>
			struct hook_to_detour_hook_helper
			{
				static inline detour_hook m_detour_hook;
			};

		public:
			template<typename T>
			static T get_original(void* target)
			{
				if (!m_target_func_to_detour_hook.contains(target))
				{
					LOG(FATAL) << "This is fucked.";
					Logger::FlushQueue();
				}

				return m_target_func_to_detour_hook[target].get_original<T>();
			}

			static void add(const std::string& name, void* target, void* detour)
			{
				if (m_target_func_to_detour_hook.contains(target))
				{
					// In our case since the detour is a central detour we only want one.
					return;
				}

				m_target_func_to_detour_hook.emplace(target, detour_hook(name, nullptr, detour));

				detour_hook_helper d{};
				d.m_detour_hook = &m_target_func_to_detour_hook[target];

				d.m_detour_hook->set_target_and_create_hook(target);

				d.enable_hook_if_hooking_is_already_running();

				m_detour_hook_helpers.push_back(d);
			}

			template<auto detour_function>
			static void add_now(const std::string& name, void* target)
			{
				hook_to_detour_hook_helper<detour_function>::m_detour_hook.set_instance(name, target, detour_function);

				detour_hook_helper d{};
				d.m_detour_hook = &hook_to_detour_hook_helper<detour_function>::m_detour_hook;

				d.enable_now();

				m_detour_hook_helpers.push_back(d);
			}

			template<auto detour_function>
			static void add(const std::string& name, void* target)
			{
				hook_to_detour_hook_helper<detour_function>::m_detour_hook.set_instance(name, target, detour_function);

				detour_hook_helper d{};
				d.m_detour_hook = &hook_to_detour_hook_helper<detour_function>::m_detour_hook;

				d.enable_hook_if_hooking_is_already_running();

				m_detour_hook_helpers.push_back(d);
			}

			template<auto detour_function>
			static void* add_lazy(const std::string& name, detour_hook_helper::ret_ptr_fn on_hooking_available)
			{
				hook_to_detour_hook_helper<detour_function>::m_detour_hook.set_instance(name, detour_function);

				detour_hook_helper d{};
				d.m_detour_hook          = &hook_to_detour_hook_helper<detour_function>::m_detour_hook;
				d.m_on_hooking_available = on_hooking_available;

				d.enable_hook_if_hooking_is_already_running();

				m_detour_hook_helpers.push_back(d);

				return nullptr;
			}

			~detour_hook_helper();
		};

		template<auto detour_function>
		static auto get_original()
		{
			return detour_hook_helper::hook_to_detour_hook_helper<detour_function>::m_detour_hook.get_original<decltype(detour_function)>();
		}

	private:
		bool m_enabled{};

		static inline std::vector<detour_hook_helper> m_detour_hook_helpers;
	};

	inline hooking* g_hooking{};
} // namespace big
