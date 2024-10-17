#pragma once

#include "hades2/disable_sgg_analytics/disable_sgg_analytics.hpp"
#include "hades2/hades_lua.hpp"
#include "hades2/log_write.hpp"
#include "hades2/sgg_exception_handler/disable_sgg_handler.hpp"
#include "memory/gm_address.hpp"

#include <config/config.hpp>

namespace big::hades
{

	inline void init_hooks()
	{
		g_hook_log_write_enabled = big::config::general().bind("Logging", "Output Vanilla Game Log", true, "Output to the Hell2Modding log the vanilla game log Hades2.log");
		hooking::detour_hook_helper::add<hook_log_write>("game logger", big::hades2_symbol_to_address["Log::Write"].as<void*>());

		const auto backtraceHandleException = big::hades2_symbol_to_address["sgg::BacktraceHandleException"];
		if (backtraceHandleException)
		{
			hooking::detour_hook_helper::add<hook_sgg_BacktraceHandleException>("Suppress SGG BacktraceHandleException", backtraceHandleException);
		}

		hooking::detour_hook_helper::add<hook_sgg_ForgeRenderer_PrintErrorMessageAndAssert>(
		    "sgg_ForgeRenderer_PrintErrorMessageAndAssert",
		    big::hades2_symbol_to_address["sgg::ForgeRenderer::PrintErrorMessageAndAssert"]);

		big::hades::lua::init_hooks();
	}
} // namespace big::hades
