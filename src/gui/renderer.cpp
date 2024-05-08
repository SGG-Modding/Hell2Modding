#include "gui/renderer.hpp"

#include "file_manager/file_manager.hpp"
#include "fonts/fonts.hpp"
#include "gui.hpp"
#include "hooks/hooking.hpp"
#include "pointers.hpp"

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <lua/lua_manager.hpp>
#include <memory/gm_address.hpp>
#include <typeinfo>
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

static IDXGIFactory4* gDxgiFactory                 = nullptr;
static ID3D12Device* gPd3DDevice                   = nullptr;
static IDXGISwapChain3* gPSwapChain                = nullptr;
static ID3D12DescriptorHeap* gPd3DRtvDescHeap      = nullptr;
static ID3D12DescriptorHeap* gPd3DSrvDescHeap      = nullptr;
static ID3D12CommandQueue* gPd3DCommandQueue       = nullptr;
static ID3D12GraphicsCommandList* gPd3DCommandList = nullptr;
static std::vector<ID3D12CommandAllocator*> gCommandAllocators;
static std::vector<ID3D12Resource*> gMainRenderTargetResource;
static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> gMainRenderTargetDescriptor;

static int get_correct_dxgi_format(int current_format)
{
	switch (current_format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
	}

	return current_format;
}

static void CreateRenderTarget(IDXGISwapChain* pSwapChain, int buffer_count)
{
	gCommandAllocators.resize(buffer_count);
	gMainRenderTargetResource.resize(buffer_count);
	gMainRenderTargetDescriptor.resize(buffer_count);

	for (UINT i = 0; i < buffer_count; ++i)
	{
		ID3D12Resource* backBuffer = nullptr;
		HRESULT buffer             = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(buffer))
		{
			DXGI_SWAP_CHAIN_DESC chainDesc;
			pSwapChain->GetDesc(&chainDesc);

			D3D12_RENDER_TARGET_VIEW_DESC desc = {};
			desc.Format        = static_cast<DXGI_FORMAT>(get_correct_dxgi_format(chainDesc.BufferDesc.Format));
			desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

			gPd3DDevice->CreateRenderTargetView(backBuffer, &desc, gMainRenderTargetDescriptor[i]);

			gMainRenderTargetResource[i] = backBuffer;
		}
		else
		{
			LOG(FATAL) << "Failed getting buffer " << i;
		}
	}
}

static void ReleaseRenderTargetResources()
{
	for (auto& resource : gMainRenderTargetResource)
	{
		if (resource)
		{
			resource->Release();
			resource = nullptr;
		}
	}
}

static void CleanupDeviceD3D12()
{
	ReleaseRenderTargetResources();

	if (gPSwapChain)
	{
		gPSwapChain->Release();
		gPSwapChain = nullptr;
	}

	for (auto& g_commandAllocator : gCommandAllocators)
	{
		if (g_commandAllocator)
		{
			g_commandAllocator->Release();
			g_commandAllocator = nullptr;
		}
	}

	if (gPd3DCommandList)
	{
		gPd3DCommandList->Release();
		gPd3DCommandList = nullptr;
	}
	if (gPd3DDevice)
	{
		gPd3DDevice->Release();
		gPd3DDevice = nullptr;
	}
	if (gDxgiFactory)
	{
		gDxgiFactory->Release();
		gDxgiFactory = nullptr;
	}

	auto ReleaseDescriptors = [](ID3D12DescriptorHeap*& heap)
	{
		if (heap)
		{
			heap->Release();
			heap = nullptr;
		}
	};

	ReleaseDescriptors(gPd3DRtvDescHeap);
	ReleaseDescriptors(gPd3DSrvDescHeap);
}

static void BuildRendererUserData(IDXGISwapChain3* pSwapChain, int buffer_count)
{
	gCommandAllocators.resize(buffer_count);
	gMainRenderTargetResource.resize(buffer_count);
	gMainRenderTargetDescriptor.resize(buffer_count);

	if (SUCCEEDED(pSwapChain->GetDevice(IID_PPV_ARGS(&gPd3DDevice))))
	{
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			desc.NodeMask                   = 1;
			desc.NumDescriptors             = buffer_count;
			desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			if (gPd3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gPd3DRtvDescHeap)) != S_OK)
			{
				return;
			}

			SIZE_T rtvDescriptorSize = gPd3DDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = gPd3DRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
			for (auto& cpuHandle : gMainRenderTargetDescriptor)
			{
				cpuHandle      = rtvHandle;
				rtvHandle.ptr += rtvDescriptorSize;
			}
		}

		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			desc.NumDescriptors             = 1;
			desc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			if (gPd3DDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&gPd3DSrvDescHeap)) != S_OK)
			{
				return;
			}
		}

		for (auto& gCommandAllocator : gCommandAllocators)
		{
			if (gPd3DDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gCommandAllocator)) != S_OK)
			{
				return;
			}
		}

		if (gPd3DDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gCommandAllocators[0], nullptr, IID_PPV_ARGS(&gPd3DCommandList)) != S_OK
		    || gPd3DCommandList->Close() != S_OK)
		{
			return;
		}

		ImGui_ImplDX12_Init(gPd3DDevice,
		                    buffer_count,
		                    DXGI_FORMAT_R8G8B8A8_UNORM,
		                    gPd3DSrvDescHeap,
		                    gPd3DSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		                    gPd3DSrvDescHeap->GetGPUDescriptorHandleForHeapStart());
	}
}

static HRESULT WINAPI hook_ResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_ResizeBuffers>()(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

static HRESULT WINAPI hook_Present(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags)
{
	if (((Flags & (UINT)DXGI_PRESENT_TEST) != (UINT)DXGI_PRESENT_TEST))
	{
		big::g_renderer->render_imgui(pSwapChain);
	}

	const auto res = big::g_hooking->get_original<hook_Present>()(pSwapChain, SyncInterval, Flags);

	return res;
}

static HRESULT WINAPI hook_Present1(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT PresentFlags, const DXGI_PRESENT_PARAMETERS* pPresentParameters)
{
	if (((PresentFlags & (UINT)DXGI_PRESENT_TEST) != (UINT)DXGI_PRESENT_TEST))
	{
		big::g_renderer->render_imgui(pSwapChain);
	}

	const auto res = big::g_hooking->get_original<hook_Present1>()(pSwapChain, SyncInterval, PresentFlags, pPresentParameters);

	return res;
}

static HRESULT WINAPI hook_ResizeBuffers1(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags, const UINT* pCreationNodeMask, IUnknown* const* ppPresentQueue)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_ResizeBuffers1>()(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
}

static HRESULT WINAPI hook_CreateSwapChain(IDXGIFactory* pFactory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc, IDXGISwapChain** ppSwapChain)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_CreateSwapChain>()(pFactory, pDevice, pDesc, ppSwapChain);
}

static HRESULT WINAPI hook_CreateSwapChainForHwnd(IDXGIFactory* pFactory, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_CreateSwapChainForHwnd>()(pFactory, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
}

static HRESULT WINAPI hook_CreateSwapChainForCoreWindow(IDXGIFactory* pFactory, IUnknown* pDevice, IUnknown* pWindow, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_CreateSwapChainForCoreWindow>()(pFactory, pDevice, pWindow, pDesc, pRestrictToOutput, ppSwapChain);
}

static HRESULT WINAPI hook_CreateSwapChainForComposition(IDXGIFactory* pFactory, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc, IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain)
{
	ReleaseRenderTargetResources();

	return big::g_hooking->get_original<hook_CreateSwapChainForComposition>()(pFactory, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
}

static void WINAPI hook_ExecuteCommandLists(ID3D12CommandQueue* pCommandQueue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists)
{
	gPd3DCommandQueue = pCommandQueue;

	return big::g_hooking->get_original<hook_ExecuteCommandLists>()(pCommandQueue, NumCommandLists, ppCommandLists);
}

IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace big
{
	static LRESULT static_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		if (g_running)
		{
			g_renderer->wndproc(hwnd, msg, wparam, lparam);
		}

		return CallWindowProcW(g_renderer->m_og_wndproc, hwnd, msg, wparam, lparam);
	}

	void renderer::wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		for (const auto& cb : m_wndproc_callbacks)
		{
			cb(hwnd, msg, wparam, lparam);
		}

		ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
	}

	void CreateImGuiForWindow(HWND window_handle)
	{
		if (ImGui::GetCurrentContext())
		{
			return;
		}

		ImGui::CreateContext();
		ImGui_ImplWin32_Init(window_handle);
	}

	static std::recursive_mutex g_imgui_draw_data_mutex;

	static void render_imgui_frame()
	{
		if (ImGui::GetCurrentContext() && gPd3DCommandQueue && gMainRenderTargetResource[0])
		{
			if (!g_imgui_draw_data_mutex.try_lock())
			{
				return;
			}

			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();

			ImGui::NewFrame();

			for (const auto& cb : g_renderer->m_dx_callbacks)
			{
				cb.m_callback();
			}

			ImGui::Render();

			g_imgui_draw_data_mutex.unlock();
		}
	}

	static void hook_sgg_scriptmanager_update_for_imgui_callbacks(float a)
	{
		render_imgui_frame();

		big::hooking::get_original<hook_sgg_scriptmanager_update_for_imgui_callbacks>()(a);
	}

	bool renderer::hook()
	{
		IDXGISwapChain1* swap_chain1{nullptr};
		IDXGISwapChain3* swap_chain{nullptr};
		ID3D12Device* device{nullptr};

		D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
		DXGI_SWAP_CHAIN_DESC1 swap_chain_desc1;

		ZeroMemory(&swap_chain_desc1, sizeof(swap_chain_desc1));

		swap_chain_desc1.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
		swap_chain_desc1.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swap_chain_desc1.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		swap_chain_desc1.BufferCount      = 2;
		swap_chain_desc1.SampleDesc.Count = 1;
		swap_chain_desc1.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
		swap_chain_desc1.Width            = 1;
		swap_chain_desc1.Height           = 1;

		// Manually get D3D12CreateDevice export because the user may be running Windows 7
		const auto d3d12_module = LoadLibraryA("d3d12.dll");
		if (d3d12_module == nullptr)
		{
			LOG(FATAL) << "Failed to load d3d12.dll";
			return false;
		}

		auto d3d12_create_device = (decltype(D3D12CreateDevice)*)GetProcAddress(d3d12_module, "D3D12CreateDevice");
		if (d3d12_create_device == nullptr)
		{
			LOG(FATAL) << "Failed to get D3D12CreateDevice export";
			return false;
		}

		LOG(INFO) << "Creating dummy device";

		if (FAILED(d3d12_create_device(nullptr, feature_level, IID_PPV_ARGS(&device))))
		{
			LOG(FATAL) << "Failed to create D3D12 Dummy device";
			return false;
		}

		LOG(INFO) << "Dummy device: " << HEX_TO_UPPER(device);

		// Manually get CreateDXGIFactory export because the user may be running Windows 7
		const auto dxgi_module = LoadLibraryA("dxgi.dll");
		if (dxgi_module == nullptr)
		{
			LOG(FATAL) << "Failed to load dxgi.dll";
			return false;
		}

		auto create_dxgi_factory = (decltype(CreateDXGIFactory)*)GetProcAddress(dxgi_module, "CreateDXGIFactory");

		if (create_dxgi_factory == nullptr)
		{
			LOG(FATAL) << "Failed to get CreateDXGIFactory export";
			return false;
		}

		LOG(INFO) << "Creating dummy DXGI factory";

		IDXGIFactory4* factory{nullptr};
		if (FAILED(create_dxgi_factory(IID_PPV_ARGS(&factory))))
		{
			LOG(FATAL) << "Failed to create D3D12 Dummy DXGI Factory";
			return false;
		}

		D3D12_COMMAND_QUEUE_DESC queue_desc{};
		queue_desc.Type     = D3D12_COMMAND_LIST_TYPE_DIRECT;
		queue_desc.Priority = 0;
		queue_desc.Flags    = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queue_desc.NodeMask = 0;

		LOG(INFO) << "Creating dummy command queue";

		ID3D12CommandQueue* command_queue{nullptr};
		if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue))))
		{
			LOG(FATAL) << "Failed to create D3D12 Dummy Command Queue";
			return false;
		}

		LOG(INFO) << "Creating dummy swapchain";

		// used in CreateSwapChainForHwnd fallback
		HWND hwnd = 0;
		WNDCLASSEX wc{};

		auto init_dummy_window = [&]()
		{
			// fallback to CreateSwapChainForHwnd
			wc.cbSize        = sizeof(WNDCLASSEX);
			wc.style         = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc   = DefWindowProc;
			wc.cbClsExtra    = 0;
			wc.cbWndExtra    = 0;
			wc.hInstance     = GetModuleHandle(NULL);
			wc.hIcon         = NULL;
			wc.hCursor       = NULL;
			wc.hbrBackground = NULL;
			wc.lpszMenuName  = NULL;
			wc.lpszClassName = TEXT("H2M_DX12_DUMMY");
			wc.hIconSm       = NULL;

			::RegisterClassEx(&wc);

			hwnd = ::CreateWindow(wc.lpszClassName, TEXT("H2M Dummy Window"), WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);

			swap_chain_desc1.BufferCount        = 3;
			swap_chain_desc1.Width              = 0;
			swap_chain_desc1.Height             = 0;
			swap_chain_desc1.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
			swap_chain_desc1.Flags              = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
			swap_chain_desc1.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc1.SampleDesc.Count   = 1;
			swap_chain_desc1.SampleDesc.Quality = 0;
			swap_chain_desc1.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swap_chain_desc1.AlphaMode          = DXGI_ALPHA_MODE_UNSPECIFIED;
			swap_chain_desc1.Scaling            = DXGI_SCALING_STRETCH;
			swap_chain_desc1.Stereo             = FALSE;
		};

		std::vector<std::function<bool()>> swapchain_attempts{
		    // we call CreateSwapChainForComposition instead of CreateSwapChainForHwnd
		    // because some overlays will have hooks on CreateSwapChainForHwnd
		    // and all we're doing is creating a dummy swapchain
		    // we don't want to screw up the overlay
		    [&]()
		    {
			    return !FAILED(factory->CreateSwapChainForComposition(command_queue, &swap_chain_desc1, nullptr, &swap_chain1));
		    },
		    [&]()
		    {
			    init_dummy_window();

			    return !FAILED(factory->CreateSwapChainForHwnd(command_queue, hwnd, &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
		    },
		    [&]()
		    {
			    return !FAILED(factory->CreateSwapChainForHwnd(command_queue, GetDesktopWindow(), &swap_chain_desc1, nullptr, nullptr, &swap_chain1));
		    },
		};

		bool any_succeed = false;

		for (auto i = 0; i < swapchain_attempts.size(); i++)
		{
			auto& attempt = swapchain_attempts[i];

			try
			{
				LOG(INFO) << "Trying swapchain attempt " << i;

				if (attempt())
				{
					LOG(INFO) << "Created dummy swapchain on attempt " << i;
					any_succeed = true;
					break;
				}
			}
			catch (std::exception& e)
			{
				LOG(FATAL) << "Failed to create dummy swapchain on attempt " << i << " " << e.what();
			}
			catch (...)
			{
				LOG(FATAL) << "Failed to create dummy swapchain on attempt " << i;
			}

			LOG(FATAL) << "Attempt failed " << i;
		}

		if (!any_succeed)
		{
			LOG(FATAL) << "Failed to create D3D12 Dummy Swap Chain";

			if (hwnd)
			{
				::DestroyWindow(hwnd);
			}

			if (wc.lpszClassName != nullptr)
			{
				::UnregisterClass(wc.lpszClassName, wc.hInstance);
			}

			return false;
		}

		LOG(INFO) << "Querying dummy swapchain";

		if (FAILED(swap_chain1->QueryInterface(IID_PPV_ARGS(&swap_chain))))
		{
			LOG(FATAL) << "Failed to retrieve D3D12 DXGI SwapChain";
			return false;
		}

		try
		{
			const auto& ti = typeid(swap_chain1);

			const std::string swapchain_classname = ti.name() ? ti.name() : "";

			LOG(INFO) << "swapchain classname: " << swapchain_classname;
		}
		catch (const std::exception& e)
		{
			LOG(FATAL) << "Failed to get type info: " << e.what();
		}
		catch (...)
		{
			LOG(FATAL) << "Failed to get type info: unknown exception";
		}

		LOG(INFO) << "Finding command queue offset";

		size_t m_command_queue_offset = 0;

		// Find the command queue offset in the swapchain
		for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*))
		{
			const auto base = (uintptr_t)swap_chain1 + i;

			// reached the end
			if (IsBadReadPtr((void*)base, sizeof(void*)))
			{
				break;
			}

			auto data = *(ID3D12CommandQueue**)base;

			if (data == command_queue)
			{
				m_command_queue_offset = i;
				LOG(INFO) << "Found command queue offset: " << i;
				break;
			}
		}

		auto target_swapchain = swap_chain;

		// Scan throughout the swapchain for a valid pointer to scan through
		// this is usually only necessary for Proton
		if (m_command_queue_offset == 0)
		{
			bool should_break = false;

			for (auto base = 0; base < 512 * sizeof(void*); base += sizeof(void*))
			{
				const auto pre_scan_base = (uintptr_t)swap_chain1 + base;

				// reached the end
				if (IsBadReadPtr((void*)pre_scan_base, sizeof(void*)))
				{
					break;
				}

				const auto scan_base = *(uintptr_t*)pre_scan_base;

				if (scan_base == 0 || IsBadReadPtr((void*)scan_base, sizeof(void*)))
				{
					continue;
				}

				for (auto i = 0; i < 512 * sizeof(void*); i += sizeof(void*))
				{
					const auto pre_data = scan_base + i;

					if (IsBadReadPtr((void*)pre_data, sizeof(void*)))
					{
						break;
					}

					auto data = *(ID3D12CommandQueue**)pre_data;

					if (data == command_queue)
					{
						// If we hook Streamline's Swapchain, the menu fails to render correctly/flickers
						// So we switch out the swapchain with the internal one owned by Streamline
						// Side note: Even though we are scanning for Proton here,
						// this doubles as an offset scanner for the real swapchain inside Streamline (or FSR3)
						//if (m_using_frame_generation_swapchain)
						{
							target_swapchain = (IDXGISwapChain3*)scan_base;
						}

						//if (!m_using_frame_generation_swapchain)
						{
							//m_using_proton_swapchain = true;
						}

						m_command_queue_offset = i;
						//m_proton_swapchain_offset = base;
						should_break = true;

						LOG(INFO) << "Proton potentially detected";
						LOG(INFO) << "Found command queue offset: " << i;
						break;
					}
				}

				//if (m_using_proton_swapchain || should_break)
				if (should_break)
				{
					break;
				}
			}
		}

		if (m_command_queue_offset == 0)
		{
			LOG(FATAL) << "Failed to find command queue offset";
			return false;
		}

		//void** swapchain_vtable     = *reinterpret_cast<void***>(gPSwapChain);
		void** swapchain_vtable = *reinterpret_cast<void***>(target_swapchain);
		//void** dxgi_factory_vtable  = *reinterpret_cast<void***>(gDxgiFactory);
		void** dxgi_factory_vtable = *reinterpret_cast<void***>(factory);
		//void** command_queue_vtable = *reinterpret_cast<void***>(gPd3DCommandQueue);
		void** command_queue_vtable = *reinterpret_cast<void***>(command_queue);

		//utility::ThreadSuspender suspender{};

		/*try
		{
			spdlog::info("Initializing hooks");

			m_present_hook.reset();
			m_swapchain_hook.reset();

			m_is_phase_1 = true;

			auto& present_fn = (*(void***)target_swapchain)[8]; // Present
			m_present_hook   = std::make_unique<PointerHook>(&present_fn, (void*)&D3D12Hook::present);
			m_hooked         = true;
		}
		catch (const std::exception& e)
		{
			spdlog::error("Failed to initialize hooks: {}", e.what());
			m_hooked = false;
		}*/

		device->Release();
		command_queue->Release();
		factory->Release();
		swap_chain1->Release();
		swap_chain->Release();

		if (hwnd)
		{
			::DestroyWindow(hwnd);
		}

		if (wc.lpszClassName != nullptr)
		{
			::UnregisterClass(wc.lpszClassName, wc.hInstance);
		}

		hooking::detour_hook_helper::add<hook_CreateSwapChain>("CSC", dxgi_factory_vtable[10]);
		hooking::detour_hook_helper::add<hook_CreateSwapChainForHwnd>("CSCFH", dxgi_factory_vtable[15]);
		hooking::detour_hook_helper::add<hook_CreateSwapChainForCoreWindow>("CSCFCW", dxgi_factory_vtable[16]);
		hooking::detour_hook_helper::add<hook_CreateSwapChainForComposition>("CSCFC", dxgi_factory_vtable[24]);

		hooking::detour_hook_helper::add<hook_Present>("P", swapchain_vtable[8]);
		hooking::detour_hook_helper::add<hook_Present1>("P1", swapchain_vtable[22]);

		hooking::detour_hook_helper::add<hook_ResizeBuffers>("RB", swapchain_vtable[13]);
		hooking::detour_hook_helper::add<hook_ResizeBuffers1>("RB1", swapchain_vtable[39]);

		hooking::detour_hook_helper::add<hook_ExecuteCommandLists>("ECL", command_queue_vtable[10]);

		hooking::detour_hook_helper::add<hook_sgg_scriptmanager_update_for_imgui_callbacks>(
		    "SGG Script Manager Update - ImGui Callbacks",
		    gmAddress::scan("4C 3B D6 74 3A", "SGG Script Manager Update").offset(-0x1'03).as<PVOID>());

		return true;
	}

	void renderer::init_fonts()
	{
		folder windows_fonts(std::filesystem::path(std::getenv("SYSTEMROOT")) / "Fonts");

		file font_file_path = windows_fonts.get_file("./msyh.ttc");
		if (!font_file_path.exists())
		{
			font_file_path = windows_fonts.get_file("./msyh.ttf");
		}
		auto font_file            = std::ifstream(font_file_path.get_path(), std::ios::binary | std::ios::ate);
		const auto font_data_size = static_cast<int>(font_file.tellg());
		const auto font_data      = std::make_unique<uint8_t[]>(font_data_size);

		font_file.seekg(0);
		font_file.read(reinterpret_cast<char*>(font_data.get()), font_data_size);
		font_file.close();

		auto& io = ImGui::GetIO();

		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;

		{
			ImFontConfig fnt_cfg{};
			fnt_cfg.FontDataOwnedByAtlas = false;
			strcpy(fnt_cfg.Name, "Fnt20px");

			io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font_storopia),
			                               sizeof(font_storopia),
			                               20.f,
			                               &fnt_cfg,
			                               io.Fonts->GetGlyphRangesDefault());
			fnt_cfg.MergeMode = true;
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 20.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 20.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
			io.Fonts->Build();
		}

		{
			ImFontConfig fnt_cfg{};
			fnt_cfg.FontDataOwnedByAtlas = false;
			strcpy(fnt_cfg.Name, "Fnt28px");

			font_title = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font_storopia), sizeof(font_storopia), 28.f, &fnt_cfg);
			fnt_cfg.MergeMode = true;
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 28.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 28.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
			io.Fonts->Build();
		}

		{
			ImFontConfig fnt_cfg{};
			fnt_cfg.FontDataOwnedByAtlas = false;
			strcpy(fnt_cfg.Name, "Fnt24px");

			font_sub_title = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font_storopia), sizeof(font_storopia), 24.f, &fnt_cfg);
			fnt_cfg.MergeMode = true;
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 24.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 24.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
			io.Fonts->Build();
		}

		{
			ImFontConfig fnt_cfg{};
			fnt_cfg.FontDataOwnedByAtlas = false;
			strcpy(fnt_cfg.Name, "Fnt18px");

			font_small = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font_storopia), sizeof(font_storopia), 18.f, &fnt_cfg);
			fnt_cfg.MergeMode = true;
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 18.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesChineseSimplifiedCommon());
			io.Fonts->AddFontFromMemoryTTF(font_data.get(), font_data_size, 18.f, &fnt_cfg, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());
			io.Fonts->Build();
		}

		{
			ImFontConfig font_icons_cfg{};
			font_icons_cfg.FontDataOwnedByAtlas = false;
			std::strcpy(font_icons_cfg.Name, "Icons");
			font_icon = io.Fonts->AddFontFromMemoryTTF(const_cast<uint8_t*>(font_icons), sizeof(font_icons), 24.f, &font_icons_cfg);
		}
	}

	void renderer::init()
	{
		//hook();
	}

	renderer::renderer()
	{
		g_renderer = this;

		init();
	}

	renderer::~renderer()
	{
		g_renderer = nullptr;
	}

	bool renderer::add_init_callback(init_callback callback)
	{
		m_init_callbacks.push_back(callback);

		return true;
	}

	bool renderer::add_dx_callback(dx_callback callback)
	{
		m_dx_callbacks.push_back(callback);

		std::sort(m_dx_callbacks.begin(),
		          m_dx_callbacks.end(),
		          [](dx_callback& a, dx_callback& b)
		          {
			          return a.m_priority < b.m_priority;
		          });

		return true;
	}

	void renderer::remove_wndproc_callback(size_t callback_index)
	{
		m_wndproc_callbacks.erase(m_wndproc_callbacks.begin() + callback_index);
	}

	size_t renderer::add_wndproc_callback(wndproc_callback callback)
	{
		m_wndproc_callbacks.emplace_back(callback);

		// Returns index of the just added element.

		return m_wndproc_callbacks.size() - 1;
	}

	struct ImDrawDataSnapshotEntry
	{
		ImDrawList* SrcCopy = NULL; // Drawlist owned by main context
		ImDrawList* OurCopy = NULL; // Our copy
		double LastUsedTime = 0.0;
	};

	struct ImDrawDataSnapshot
	{
		// Members
		ImDrawData DrawData;
		ImPool<ImDrawDataSnapshotEntry> Cache;
		float MemoryCompactTimer = 20.0f; // Discard unused data after 20 seconds

		// Functions
		~ImDrawDataSnapshot()
		{
			Clear();
		}

		void Clear();
		void SnapUsingSwap(ImDrawData* src, double current_time); // Efficient snapshot by swapping data, meaning "src_list" is unusable.

		//void                          SnapUsingCopy(ImDrawData* src, double current_time); // Deep-copy snapshop

		// Internals
		ImGuiID GetDrawListID(ImDrawList* src_list)
		{
			return ImHashData(&src_list, sizeof(src_list));
		} // Hash pointer

		ImDrawDataSnapshotEntry* GetOrAddEntry(ImDrawList* src_list)
		{
			return Cache.GetOrAddByKey(GetDrawListID(src_list));
		}
	};

	void ImDrawDataSnapshot::Clear()
	{
		for (int n = 0; n < Cache.GetMapSize(); n++)
		{
			if (ImDrawDataSnapshotEntry* entry = Cache.TryGetMapData(n))
			{
				IM_DELETE(entry->OurCopy);
			}
		}
		Cache.Clear();
		DrawData.Clear();
	}

	void ImDrawDataSnapshot::SnapUsingSwap(ImDrawData* src, double current_time)
	{
		ImDrawData* dst = &DrawData;
		IM_ASSERT(src != dst && src->Valid);

		// Copy all fields except CmdLists[]
		ImVector<ImDrawList*> backup_draw_list;
		backup_draw_list.swap(src->CmdLists);

		*dst = *src;
		backup_draw_list.swap(src->CmdLists);

		// Swap and mark as used
		for (ImDrawList* src_list : src->CmdLists)
		{
			ImDrawDataSnapshotEntry* entry = GetOrAddEntry(src_list);
			if (entry->OurCopy == NULL)
			{
				entry->SrcCopy = src_list;
				entry->OurCopy = IM_NEW(ImDrawList)(src_list->_Data);
			}
			IM_ASSERT(entry->SrcCopy == src_list);
			entry->SrcCopy->CmdBuffer.swap(entry->OurCopy->CmdBuffer); // Cheap swap
			entry->SrcCopy->IdxBuffer.swap(entry->OurCopy->IdxBuffer);
			entry->SrcCopy->VtxBuffer.swap(entry->OurCopy->VtxBuffer);
			entry->SrcCopy->CmdBuffer.reserve(entry->OurCopy->CmdBuffer.Capacity); // Preserve bigger size to avoid reallocs for two consecutive frames
			entry->SrcCopy->IdxBuffer.reserve(entry->OurCopy->IdxBuffer.Capacity);
			entry->SrcCopy->VtxBuffer.reserve(entry->OurCopy->VtxBuffer.Capacity);
			entry->LastUsedTime = current_time;
			dst->CmdLists.push_back(entry->OurCopy);
		}

		// Cleanup unused data
		const double gc_threshold = current_time - MemoryCompactTimer;
		for (int n = 0; n < Cache.GetMapSize(); n++)
		{
			if (ImDrawDataSnapshotEntry* entry = Cache.TryGetMapData(n))
			{
				if (entry->LastUsedTime > gc_threshold)
				{
					continue;
				}
				IM_DELETE(entry->OurCopy);
				Cache.Remove(GetDrawListID(entry->SrcCopy), entry);
			}
		}
	};

	void renderer::render_imgui(IDXGISwapChain3* pSwapChain)
	{
		static bool init = true;
		if (init)
		{
			init = false;

			pSwapChain->GetHwnd(&g_renderer->m_window_handle);
			CreateImGuiForWindow(g_renderer->m_window_handle);

			auto file_path                             = g_file_manager.get_project_file("./imgui.ini").get_path();
			static std::string path                    = file_path.make_preferred().string();
			ImGui::GetCurrentContext()->IO.IniFilename = path.c_str();

			g_renderer->init_fonts();

			static gui g_gui{};

			for (const auto& init_cb : g_renderer->m_init_callbacks)
			{
				if (init_cb)
				{
					init_cb();
				}
			}

			g_renderer->m_og_wndproc = WNDPROC(SetWindowLongPtrW(g_renderer->m_window_handle, GWLP_WNDPROC, LONG_PTR(&static_wndproc)));

			LOG(INFO) << "made it";
		}

		if (!ImGui::GetIO().BackendRendererUserData)
		{
			DXGI_SWAP_CHAIN_DESC sdesc;
			pSwapChain->GetDesc(&sdesc);
			BuildRendererUserData(pSwapChain, sdesc.BufferCount);
		}

		if (!gMainRenderTargetResource[0])
		{
			DXGI_SWAP_CHAIN_DESC sdesc;
			pSwapChain->GetDesc(&sdesc);
			CreateRenderTarget(pSwapChain, sdesc.BufferCount);
		}

		if (ImGui::GetCurrentContext() && gPd3DCommandQueue && gMainRenderTargetResource[0])
		{
			static ImDrawDataSnapshot snapshot;
			auto draw_data_to_render = ImGui::GetDrawData();

			bool locked_mutex = false;
			if (draw_data_to_render)
			{
				if (g_imgui_draw_data_mutex.try_lock())
				{
					//return;

					locked_mutex = true;

					snapshot.SnapUsingSwap(draw_data_to_render, ImGui::GetTime());
					draw_data_to_render = &snapshot.DrawData;
				}
				else
				{
					draw_data_to_render = &snapshot.DrawData;
				}
			}
			else
			{
				draw_data_to_render = &snapshot.DrawData;
			}

			UINT backBufferIdx                       = pSwapChain->GetCurrentBackBufferIndex();
			ID3D12CommandAllocator* commandAllocator = gCommandAllocators[backBufferIdx];
			commandAllocator->Reset();

			D3D12_RESOURCE_BARRIER barrier = {};
			barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource   = gMainRenderTargetResource[backBufferIdx];
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			gPd3DCommandList->Reset(commandAllocator, nullptr);
			gPd3DCommandList->ResourceBarrier(1, &barrier);

			gPd3DCommandList->OMSetRenderTargets(1, &gMainRenderTargetDescriptor[backBufferIdx], FALSE, nullptr);
			gPd3DCommandList->SetDescriptorHeaps(1, &gPd3DSrvDescHeap);
			ImGui_ImplDX12_RenderDrawData(draw_data_to_render, gPd3DCommandList);
			barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			gPd3DCommandList->ResourceBarrier(1, &barrier);
			gPd3DCommandList->Close();

			gPd3DCommandQueue->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList* const*>(&gPd3DCommandList));

			if (locked_mutex)
			{
				g_imgui_draw_data_mutex.unlock();
			}
		}
	}
} // namespace big
