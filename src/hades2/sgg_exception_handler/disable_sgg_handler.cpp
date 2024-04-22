#include "disable_sgg_handler.hpp"

namespace big
{
	bool hook_sgg_BacktraceHandleException(_EXCEPTION_POINTERS* exception)
	{
		return false;
	}
} // namespace big
