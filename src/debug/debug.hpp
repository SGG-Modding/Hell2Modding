#pragma once

namespace big::debug
{
	inline void wait_until_debugger()
	{
#ifndef FINAL
		while (!IsDebuggerPresent())
		{
			std::this_thread::sleep_for(1s);
		}
#else
		LOG(VERBOSE) << "Not waiting for debugger because it's a release build.";
		Logger::FlushQueue();
#endif
	}
} // namespace big::debug
