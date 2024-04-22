#pragma once

namespace big::win_registry
{
	inline LONG get_dword(HKEY hKey, const std::wstring &strValueName, DWORD &nValue, DWORD nDefaultValue)
	{
		nValue = nDefaultValue;
		DWORD dwBufferSize(sizeof(DWORD));
		DWORD nResult(0);
		LONG nError = ::RegQueryValueExW(hKey, strValueName.c_str(), 0, NULL, reinterpret_cast<LPBYTE>(&nResult), &dwBufferSize);
		if (ERROR_SUCCESS == nError)
		{
			nValue = nResult;
		}
		return nError;
	}

	inline LONG get_bool(HKEY hKey, const std::wstring &strValueName, bool &bValue, bool bDefaultValue)
	{
		DWORD nDefValue((bDefaultValue) ? 1 : 0);
		DWORD nResult(nDefValue);
		LONG nError = get_dword(hKey, strValueName.c_str(), nResult, nDefValue);
		if (ERROR_SUCCESS == nError)
		{
			bValue = (nResult != 0) ? true : false;
		}
		return nError;
	}

	inline LONG get_wstring(HKEY hKey, const std::wstring &strValueName, std::wstring &strValue, const std::wstring &strDefaultValue)
	{
		strValue = strDefaultValue;
		WCHAR szBuffer[512];
		DWORD dwBufferSize = sizeof(szBuffer);
		ULONG nError;
		nError = RegQueryValueExW(hKey, strValueName.c_str(), 0, NULL, (LPBYTE)szBuffer, &dwBufferSize);
		if (ERROR_SUCCESS == nError)
		{
			strValue = szBuffer;
		}
		return nError;
	}
} // namespace big::win_registry
