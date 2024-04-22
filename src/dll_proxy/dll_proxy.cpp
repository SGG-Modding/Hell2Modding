#include "dll_proxy.hpp"

struct D3D12_dll
{
	HMODULE dll;
	FARPROC oD3D12CoreCreateLayeredDevice;
	FARPROC oD3D12CoreGetLayeredDeviceSize;
	FARPROC oD3D12CoreRegisterLayers;
	FARPROC oD3D12CreateDevice;
	FARPROC oD3D12CreateRootSignatureDeserializer;
	FARPROC oD3D12CreateVersionedRootSignatureDeserializer;
	FARPROC oD3D12DeviceRemovedExtendedData;
	FARPROC oD3D12EnableExperimentalFeatures;
	FARPROC oD3D12GetDebugInterface;
	FARPROC oD3D12GetInterface;
	FARPROC oD3D12PIXEventsReplaceBlock;
	FARPROC oD3D12PIXGetThreadInfo;
	FARPROC oD3D12PIXNotifyWakeFromFenceSignal;
	FARPROC oD3D12PIXReportCounter;
	FARPROC oD3D12SerializeRootSignature;
	FARPROC oD3D12SerializeVersionedRootSignature;
	FARPROC oGetBehaviorValue;
	FARPROC oSetAppCompatStringPointer;
} D3D12;

extern "C"
{
	FARPROC PA = 0;
	int runASM();

	void fD3D12CoreCreateLayeredDevice()
	{
		PA = D3D12.oD3D12CoreCreateLayeredDevice;
		runASM();
	}

	void fD3D12CoreGetLayeredDeviceSize()
	{
		PA = D3D12.oD3D12CoreGetLayeredDeviceSize;
		runASM();
	}

	void fD3D12CoreRegisterLayers()
	{
		PA = D3D12.oD3D12CoreRegisterLayers;
		runASM();
	}

	void fD3D12CreateDevice()
	{
		PA = D3D12.oD3D12CreateDevice;
		runASM();
	}

	void fD3D12CreateRootSignatureDeserializer()
	{
		PA = D3D12.oD3D12CreateRootSignatureDeserializer;
		runASM();
	}

	void fD3D12CreateVersionedRootSignatureDeserializer()
	{
		PA = D3D12.oD3D12CreateVersionedRootSignatureDeserializer;
		runASM();
	}

	void fD3D12DeviceRemovedExtendedData()
	{
		PA = D3D12.oD3D12DeviceRemovedExtendedData;
		runASM();
	}

	void fD3D12EnableExperimentalFeatures()
	{
		PA = D3D12.oD3D12EnableExperimentalFeatures;
		runASM();
	}

	void fD3D12GetDebugInterface()
	{
		PA = D3D12.oD3D12GetDebugInterface;
		runASM();
	}

	void fD3D12GetInterface()
	{
		PA = D3D12.oD3D12GetInterface;
		runASM();
	}

	void fD3D12PIXEventsReplaceBlock()
	{
		PA = D3D12.oD3D12PIXEventsReplaceBlock;
		runASM();
	}

	void fD3D12PIXGetThreadInfo()
	{
		PA = D3D12.oD3D12PIXGetThreadInfo;
		runASM();
	}

	void fD3D12PIXNotifyWakeFromFenceSignal()
	{
		PA = D3D12.oD3D12PIXNotifyWakeFromFenceSignal;
		runASM();
	}

	void fD3D12PIXReportCounter()
	{
		PA = D3D12.oD3D12PIXReportCounter;
		runASM();
	}

	void fD3D12SerializeRootSignature()
	{
		PA = D3D12.oD3D12SerializeRootSignature;
		runASM();
	}

	void fD3D12SerializeVersionedRootSignature()
	{
		PA = D3D12.oD3D12SerializeVersionedRootSignature;
		runASM();
	}

	void fGetBehaviorValue()
	{
		PA = D3D12.oGetBehaviorValue;
		runASM();
	}

	void fSetAppCompatStringPointer()
	{
		PA = D3D12.oSetAppCompatStringPointer;
		runASM();
	}
}

static void setupFunctions()
{
	D3D12.oD3D12CoreCreateLayeredDevice         = GetProcAddress(D3D12.dll, "D3D12CoreCreateLayeredDevice");
	D3D12.oD3D12CoreGetLayeredDeviceSize        = GetProcAddress(D3D12.dll, "D3D12CoreGetLayeredDeviceSize");
	D3D12.oD3D12CoreRegisterLayers              = GetProcAddress(D3D12.dll, "D3D12CoreRegisterLayers");
	D3D12.oD3D12CreateDevice                    = GetProcAddress(D3D12.dll, "D3D12CreateDevice");
	D3D12.oD3D12CreateRootSignatureDeserializer = GetProcAddress(D3D12.dll, "D3D12CreateRootSignatureDeserializer");
	D3D12.oD3D12CreateVersionedRootSignatureDeserializer = GetProcAddress(D3D12.dll, "D3D12CreateVersionedRootSignatureDeserializer");
	D3D12.oD3D12DeviceRemovedExtendedData       = GetProcAddress(D3D12.dll, "D3D12DeviceRemovedExtendedData");
	D3D12.oD3D12EnableExperimentalFeatures      = GetProcAddress(D3D12.dll, "D3D12EnableExperimentalFeatures");
	D3D12.oD3D12GetDebugInterface               = GetProcAddress(D3D12.dll, "D3D12GetDebugInterface");
	D3D12.oD3D12GetInterface                    = GetProcAddress(D3D12.dll, "D3D12GetInterface");
	D3D12.oD3D12PIXEventsReplaceBlock           = GetProcAddress(D3D12.dll, "D3D12PIXEventsReplaceBlock");
	D3D12.oD3D12PIXGetThreadInfo                = GetProcAddress(D3D12.dll, "D3D12PIXGetThreadInfo");
	D3D12.oD3D12PIXNotifyWakeFromFenceSignal    = GetProcAddress(D3D12.dll, "D3D12PIXNotifyWakeFromFenceSignal");
	D3D12.oD3D12PIXReportCounter                = GetProcAddress(D3D12.dll, "D3D12PIXReportCounter");
	D3D12.oD3D12SerializeRootSignature          = GetProcAddress(D3D12.dll, "D3D12SerializeRootSignature");
	D3D12.oD3D12SerializeVersionedRootSignature = GetProcAddress(D3D12.dll, "D3D12SerializeVersionedRootSignature");
	D3D12.oGetBehaviorValue                     = GetProcAddress(D3D12.dll, "GetBehaviorValue");
	D3D12.oSetAppCompatStringPointer            = GetProcAddress(D3D12.dll, "SetAppCompatStringPointer");
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
