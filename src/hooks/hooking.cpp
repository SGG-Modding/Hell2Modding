#include "hooks/hooking.hpp"

#include "gui/gui.hpp"
#include "gui/renderer.hpp"
#include "hades2/disable_sgg_analytics/disable_sgg_analytics.hpp"
#include "hades2/lua/sgg_lua.hpp"
#include "hades2/sgg_exception_handler/disable_sgg_handler.hpp"
#include "memory/module.hpp"
#include "pointers.hpp"
#include "threads/util.hpp"

namespace big
{
	hooking::hooking()
	{
		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			const auto is_lazy_hook = detour_hook_helper.m_on_hooking_available.operator bool();
			if (is_lazy_hook)
			{
				detour_hook_helper.m_detour_hook->set_target_and_create_hook(detour_hook_helper.m_on_hooking_available());
			}
		}

		hooking::detour_hook_helper::add<hook_luaL_checkversion_>("Multiple Lua VM detected patch", luaL_checkversion_);
		hooking::detour_hook_helper::add<hook_InitLua>("LNS", g_pointers->m_hades2.m_init_lua);
		hooking::detour_hook_helper::add<hook_sgg_ScriptManager_Clear>("SMC", g_pointers->m_hades2.m_scriptmanager_clear);

		hooking::detour_hook_helper::add<hook_sgg_BacktraceHandleException>("Suppress SGG BacktraceHandleException",
		                                                                    g_pointers->m_hades2.m_sgg_BacktraceHandleException);
		hooking::detour_hook_helper::add<hook_sgg_ForgeRenderer_PrintErrorMessageAndAssert>("HSGGFRPEMAA",
		                                                                                    g_pointers->m_hades2.m_sgg_ForgeRenderer_PrintErrorMessageAndAssert);

		g_hooking = this;
	}

	hooking::~hooking()
	{
		if (m_enabled)
		{
			disable();
		}

		g_hooking = nullptr;
	}

	void hooking::enable()
	{
		threads::suspend_all_but_one();

		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			detour_hook_helper.m_detour_hook->enable();
		}

		threads::resume_all();

		m_enabled = true;
	}

	void hooking::disable()
	{
		m_enabled = false;

		threads::suspend_all_but_one();

		for (auto& detour_hook_helper : m_detour_hook_helpers)
		{
			detour_hook_helper.m_detour_hook->disable();
		}

		threads::resume_all();

		m_detour_hook_helpers.clear();
	}

	hooking::detour_hook_helper::~detour_hook_helper()
	{
	}

	void hooking::detour_hook_helper::enable_now()
	{
		m_detour_hook->enable();
	}

	void hooking::detour_hook_helper::enable_hook_if_hooking_is_already_running()
	{
		if (g_hooking && g_hooking->m_enabled)
		{
			if (m_on_hooking_available)
			{
				m_detour_hook->set_target_and_create_hook(m_on_hooking_available());
			}

			m_detour_hook->enable();
		}
	}
} // namespace big
