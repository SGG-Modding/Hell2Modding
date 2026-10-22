#include "dll_proxy.hpp"

#include <windows.h>

struct D3D12_dll
{
	HMODULE dll;
} D3D12;

extern "C"
{
	extern FARPROC oD3D12CreateDevice;
	extern FARPROC oD3D12GetInterface;
	extern FARPROC oD3D12SerializeRootSignature;
	extern FARPROC oD3D12SerializeVersionedRootSignature;
	extern FARPROC oD3D12CreateRootSignatureDeserializer;
	extern FARPROC oD3D12CreateVersionedRootSignatureDeserializer;
	extern FARPROC oD3D12GetDebugInterface;
	extern FARPROC oD3D12DeviceRemovedExtendedData;
	extern FARPROC oD3D12EnableExperimentalFeatures;
	extern FARPROC oD3D12CoreCreateLayeredDevice;
	extern FARPROC oD3D12CoreGetLayeredDeviceSize;
	extern FARPROC oD3D12CoreRegisterLayers;
	extern FARPROC oD3D12PIXEventsReplaceBlock;
	extern FARPROC oD3D12PIXGetThreadInfo;
	extern FARPROC oD3D12PIXNotifyWakeFromFenceSignal;
	extern FARPROC oD3D12PIXReportCounter;
	extern FARPROC oSetAppCompatStringPointer;
	extern FARPROC oGetBehaviorValue;
}

static void setupFunctions()
{
	oD3D12CreateDevice                    = GetProcAddress(D3D12.dll, "D3D12CreateDevice");
	oD3D12GetInterface                    = GetProcAddress(D3D12.dll, "D3D12GetInterface");
	oD3D12SerializeRootSignature          = GetProcAddress(D3D12.dll, "D3D12SerializeRootSignature");
	oD3D12SerializeVersionedRootSignature = GetProcAddress(D3D12.dll, "D3D12SerializeVersionedRootSignature");
	oD3D12CreateRootSignatureDeserializer = GetProcAddress(D3D12.dll, "D3D12CreateRootSignatureDeserializer");
	oD3D12CreateVersionedRootSignatureDeserializer = GetProcAddress(D3D12.dll, "D3D12CreateVersionedRootSignatureDeserializer");
	oD3D12GetDebugInterface            = GetProcAddress(D3D12.dll, "D3D12GetDebugInterface");
	oD3D12DeviceRemovedExtendedData    = GetProcAddress(D3D12.dll, "D3D12DeviceRemovedExtendedData");
	oD3D12EnableExperimentalFeatures   = GetProcAddress(D3D12.dll, "D3D12EnableExperimentalFeatures");
	oD3D12CoreCreateLayeredDevice      = GetProcAddress(D3D12.dll, "D3D12CoreCreateLayeredDevice");
	oD3D12CoreGetLayeredDeviceSize     = GetProcAddress(D3D12.dll, "D3D12CoreGetLayeredDeviceSize");
	oD3D12CoreRegisterLayers           = GetProcAddress(D3D12.dll, "D3D12CoreRegisterLayers");
	oD3D12PIXEventsReplaceBlock        = GetProcAddress(D3D12.dll, "D3D12PIXEventsReplaceBlock");
	oD3D12PIXGetThreadInfo             = GetProcAddress(D3D12.dll, "D3D12PIXGetThreadInfo");
	oD3D12PIXNotifyWakeFromFenceSignal = GetProcAddress(D3D12.dll, "D3D12PIXNotifyWakeFromFenceSignal");
	oD3D12PIXReportCounter             = GetProcAddress(D3D12.dll, "D3D12PIXReportCounter");
	oSetAppCompatStringPointer         = GetProcAddress(D3D12.dll, "SetAppCompatStringPointer");
	oGetBehaviorValue                  = GetProcAddress(D3D12.dll, "GetBehaviorValue");
}

namespace big
{
	void dll_proxy::init()
	{
		BOOL wow64 = FALSE;
		WCHAR path[MAX_PATH];

		if (IsWow64Process(GetCurrentProcess(), &wow64) && wow64)
		{
			GetSystemWow64DirectoryW(path, MAX_PATH);
		}
		else
		{
			GetSystemDirectoryW(path, MAX_PATH);
		}

		lstrcatW(path, L"\\");
		lstrcatW(path, L"D3D12.dll");
		D3D12.dll = LoadLibraryW(path);

		setupFunctions();
	}

} // namespace big
