#pragma once

namespace big
{
	struct version
	{
		static const char* GIT_SHA1;
		static const char* GIT_DATE;
		static const char* GIT_COMMIT_SUBJECT;
		static const char* GIT_BRANCH;

		static inline const char* VERSION_NUMBER = "1.0.0";
	};
}