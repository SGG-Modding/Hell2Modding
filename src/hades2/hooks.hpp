#pragma once

#include "hades2/disable_sgg_analytics/disable_sgg_analytics.hpp"
#include "hades2/hades_lua.hpp"
#include "hades2/log_write.hpp"
#include "hades2/sgg_exception_handler/disable_sgg_handler.hpp"
#include "memory/gm_address.hpp"

namespace big::hades
{
	inline void init_hooks()
	{
		hooking::detour_hook_helper::add<hook_log_write>("game logger",
		                                                 gmAddress::scan("8B D1 83 E2 08", "game logger").offset(-0x2C).as<void*>());

		const auto backtraceHandleException = gmAddress::scan("B8 B0 FC 00 00", "BacktraceHandleException");
		if (backtraceHandleException)
		{
			hooking::detour_hook_helper::add<hook_sgg_BacktraceHandleException>("Suppress SGG BacktraceHandleException",
			                                                                    backtraceHandleException.offset(-0x20));
		}

		hooking::detour_hook_helper::add<hook_sgg_ForgeRenderer_PrintErrorMessageAndAssert>(
		    "sgg_ForgeRenderer_PrintErrorMessageAndAssert",
		    gmAddress::scan("48 63 44 24 34", "sgg_ForgeRenderer_PrintErrorMessageAndAssert").offset(-0x97));

		big::hades::lua::init_hooks();
	}
} // namespace big::hades
