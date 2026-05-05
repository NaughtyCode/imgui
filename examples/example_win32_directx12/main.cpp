// Dear ImGui: standalone example application for Windows API + DirectX 12
// Multi-window version: multiple native Win32 windows, each with its own ImGui context.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <tchar.h>
#include <vector>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

static const int APP_NUM_FRAMES_IN_FLIGHT = 2;
static const int APP_NUM_BACK_BUFFERS = 2;
static const int APP_SRV_HEAP_SIZE = 64;
static const int APP_MAX_WINDOWS = 4;

struct FrameContext
{
    ID3D12CommandAllocator*     CommandAllocator;
    UINT64                      FenceValue;
};

struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap*       Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu;
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu;
    UINT                        HeapHandleIncrement;
    ImVector<int>               FreeIndices;

    void Create(ID3D12Device* device, ID3D12DescriptorHeap* heap)
    {
        IM_ASSERT(Heap == nullptr && FreeIndices.empty());
        Heap = heap;
        D3D12_DESCRIPTOR_HEAP_DESC desc = heap->GetDesc();
        HeapType = desc.Type;
        HeapStartCpu = Heap->GetCPUDescriptorHandleForHeapStart();
        HeapStartGpu = Heap->GetGPUDescriptorHandleForHeapStart();
        HeapHandleIncrement = device->GetDescriptorHandleIncrementSize(HeapType);
        FreeIndices.reserve((int)desc.NumDescriptors);
        for (int n = desc.NumDescriptors; n > 0; n--)
            FreeIndices.push_back(n - 1);
    }
    void Destroy()
    {
        Heap = nullptr;
        FreeIndices.clear();
    }
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle)
    {
        IM_ASSERT(FreeIndices.Size > 0);
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu_desc_handle->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu_desc_handle->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE out_gpu_desc_handle)
    {
        int cpu_idx = (int)((out_cpu_desc_handle.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((out_gpu_desc_handle.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

// Per-window state
struct WindowContext
{
    HWND                                    Hwnd = nullptr;
    IDXGISwapChain3*                        SwapChain = nullptr;
    HANDLE                                  SwapChainWaitableObject = nullptr;
    ID3D12Resource*                         RenderTargetResource[APP_NUM_BACK_BUFFERS] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE             RenderTargetDescriptor[APP_NUM_BACK_BUFFERS] = {};
    ImGuiContext*                           ImGuiCtx = nullptr;
    bool                                    Occluded = false;
    bool                                    Closed = false;
    int                                     WindowId = 0;
    bool                                    ShowDemoWindow = false;
    bool                                    ShowAnotherWindow = false;
    ImVec4                                  ClearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
};

// Shared D3D12 state
static FrameContext                 g_frameContext[APP_NUM_FRAMES_IN_FLIGHT] = {};
static UINT                         g_frameIndex = 0;
static ID3D12Device*                g_pd3dDevice = nullptr;
static ID3D12DescriptorHeap*        g_pd3dRtvDescHeap = nullptr;
static ID3D12DescriptorHeap*        g_pd3dSrvDescHeap = nullptr;
static ExampleDescriptorHeapAllocator g_pd3dSrvDescHeapAlloc;
static ID3D12CommandQueue*          g_pd3dCommandQueue = nullptr;
static ID3D12GraphicsCommandList*   g_pd3dCommandList = nullptr;
static ID3D12Fence*                 g_fence = nullptr;
static HANDLE                       g_fenceEvent = nullptr;
static UINT64                       g_fenceLastSignaledValue = 0;
static bool                         g_SwapChainTearingSupport = false;
static std::vector<WindowContext*>  g_windows;

// Forward declarations
bool CreateDeviceD3D();
void CleanupDeviceD3D();
void CreateRenderTarget(WindowContext* wc);
void CleanupRenderTarget(WindowContext* wc);
void WaitForPendingOperations();
FrameContext* WaitForNextFrameContext();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
WindowContext* CreateWindowContext(const wchar_t* title, int x, int y, int width, int height, float scale, int id);
void DestroyWindowContext(WindowContext* wc);

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main code
int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    if (!CreateDeviceD3D())
    {
        CleanupDeviceD3D();
        return 1;
    }

    // Create three windows with distinct content
    WindowContext* wc1 = CreateWindowContext(L"ImGui Window 1 - Demo",    100, 100, 1280, 800, main_scale, 0);
    WindowContext* wc2 = CreateWindowContext(L"ImGui Window 2 - Plots",   150, 180, 800,  600, main_scale, 1);
    WindowContext* wc3 = CreateWindowContext(L"ImGui Window 3 - Settings",200, 260, 700,  500, main_scale, 2);

    if (!wc1 || !wc2 || !wc3)
    {
        if (wc1) DestroyWindowContext(wc1);
        if (wc2) DestroyWindowContext(wc2);
        if (wc3) DestroyWindowContext(wc3);
        CleanupDeviceD3D();
        return 1;
    }

    // Configure each window's initial state
    wc1->ShowDemoWindow = true;
    wc1->ClearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    wc2->ShowAnotherWindow = true;
    wc2->ClearColor = ImVec4(0.30f, 0.45f, 0.30f, 1.00f);

    wc3->ClearColor = ImVec4(0.40f, 0.35f, 0.55f, 1.00f);

    g_windows.push_back(wc1);
    g_windows.push_back(wc2);
    g_windows.push_back(wc3);

    for (WindowContext* wc : g_windows)
    {
        ::ShowWindow(wc->Hwnd, SW_SHOWDEFAULT);
        ::UpdateWindow(wc->Hwnd);
    }

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll messages for all windows
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Remove closed windows
        for (auto it = g_windows.begin(); it != g_windows.end(); )
        {
            if ((*it)->Closed)
            {
                DestroyWindowContext(*it);
                it = g_windows.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (g_windows.empty())
            break;

        // Check if all windows are occluded
        bool all_occluded = true;
        for (WindowContext* wc : g_windows)
        {
            if (!wc->Occluded && !::IsIconic(wc->Hwnd))
            {
                all_occluded = false;
                break;
            }
        }
        if (all_occluded)
        {
            ::Sleep(10);
            continue;
        }

        // Build UI for each window (each has its own ImGui context)
        for (WindowContext* wc : g_windows)
        {
            ImGui::SetCurrentContext(wc->ImGuiCtx);
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            switch (wc->WindowId)
            {
            case 0: // Window 1: Demo window + "Hello, world!"
            {
                if (wc->ShowDemoWindow)
                    ImGui::ShowDemoWindow(&wc->ShowDemoWindow);

                static float f = 0.0f;
                static int counter = 0;
                ImGuiIO& io = ImGui::GetIO();

                ImGui::Begin("Hello, world!");
                ImGui::Text("This is window 1 content.");
                ImGui::Checkbox("Demo Window", &wc->ShowDemoWindow);
                ImGui::Checkbox("Another Window", &wc->ShowAnotherWindow);
                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
                ImGui::ColorEdit3("clear color", (float*)&wc->ClearColor);
                if (ImGui::Button("Button"))
                    counter++;
                ImGui::SameLine();
                ImGui::Text("counter = %d", counter);
                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
                ImGui::End();

                if (wc->ShowAnotherWindow)
                {
                    ImGui::Begin("Another Window", &wc->ShowAnotherWindow);
                    ImGui::Text("Hello from window 1's extra window!");
                    if (ImGui::Button("Close Me"))
                        wc->ShowAnotherWindow = false;
                    ImGui::End();
                }
                break;
            }
            case 1: // Window 2: Plot widgets
            {
                ImGui::Begin("Window 2 - Plots");
                ImGui::Text("This window demonstrates plot widgets.");

                static float values[90] = {};
                static int values_offset = 0;
                static float refresh_time = 0.0f;
                ImGuiIO& io = ImGui::GetIO();
                if (refresh_time == 0.0f)
                    refresh_time = (float)ImGui::GetTime();
                while (refresh_time < ImGui::GetTime())
                {
                    values[values_offset] = sinf(refresh_time * 3.0f) * 0.5f + 0.5f;
                    values_offset = (values_offset + 1) % 90;
                    refresh_time += 1.0f / 60.0f;
                }

                ImGui::PlotLines("Sine Wave", values, 90, values_offset, nullptr, 0.0f, 1.0f, ImVec2(0, 120));
                ImGui::PlotHistogram("Histogram", values, 90, values_offset, nullptr, 0.0f, 1.0f, ImVec2(0, 120));
                ImGui::Text("FPS: %.1f", io.Framerate);

                static int slider_val = 50;
                ImGui::SliderInt("Value", &slider_val, 0, 100);
                ImGui::ProgressBar(slider_val / 100.0f);
                ImGui::End();

                if (wc->ShowAnotherWindow)
                {
                    ImGui::Begin("Window 2 - Extra", &wc->ShowAnotherWindow);
                    ImGui::Text("Another window in window 2!");
                    static bool checked = true;
                    ImGui::Checkbox("Check me", &checked);
                    ImGui::End();
                }
                break;
            }
            case 2: // Window 3: Settings-style panel
            {
                ImGui::Begin("Window 3 - Settings");
                ImGui::Text("Settings and Controls");

                static bool vsync = true;
                static bool fullscreen = false;
                static int aa_samples = 4;
                static float volume = 0.75f;
                static int resolution = 0;
                const char* resolutions[] = { "1920x1080", "2560x1440", "3840x2160" };

                ImGui::Checkbox("VSync", &vsync);
                ImGui::Checkbox("Fullscreen", &fullscreen);
                ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f);
                ImGui::Combo("Resolution", &resolution, resolutions, 3);
                ImGui::RadioButton("Low", &aa_samples, 0); ImGui::SameLine();
                ImGui::RadioButton("Medium", &aa_samples, 2); ImGui::SameLine();
                ImGui::RadioButton("High", &aa_samples, 4);

                ImGui::Separator();
                ImGui::ColorEdit3("Background", (float*)&wc->ClearColor);

                ImGuiIO& io = ImGui::GetIO();
                ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
                ImGui::Text("WantCaptureKeyboard: %s", io.WantCaptureKeyboard ? "true" : "false");
                ImGui::End();
                break;
            }
            }

            ImGui::Render();
        }

        // Render all windows using a single command list
        FrameContext* frameCtx = WaitForNextFrameContext();
        frameCtx->CommandAllocator->Reset();
        g_pd3dCommandList->Reset(frameCtx->CommandAllocator, nullptr);
        g_pd3dCommandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);

        for (WindowContext* wc : g_windows)
        {
            if (wc->Occluded || ::IsIconic(wc->Hwnd))
                continue;

            UINT backBufferIdx = wc->SwapChain->GetCurrentBackBufferIndex();

            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags                  = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource   = wc->RenderTargetResource[backBufferIdx];
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);

            const float clear_color_with_alpha[4] = {
                wc->ClearColor.x * wc->ClearColor.w,
                wc->ClearColor.y * wc->ClearColor.w,
                wc->ClearColor.z * wc->ClearColor.w,
                wc->ClearColor.w };
            g_pd3dCommandList->ClearRenderTargetView(wc->RenderTargetDescriptor[backBufferIdx], clear_color_with_alpha, 0, nullptr);
            g_pd3dCommandList->OMSetRenderTargets(1, &wc->RenderTargetDescriptor[backBufferIdx], FALSE, nullptr);

            ImGui::SetCurrentContext(wc->ImGuiCtx);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_pd3dCommandList);

            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
            g_pd3dCommandList->ResourceBarrier(1, &barrier);
        }

        g_pd3dCommandList->Close();
        g_pd3dCommandQueue->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_pd3dCommandList);
        g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
        frameCtx->FenceValue = g_fenceLastSignaledValue;

        // Present all visible windows
        for (WindowContext* wc : g_windows)
        {
            if (wc->Occluded || ::IsIconic(wc->Hwnd))
                continue;
            HRESULT hr = wc->SwapChain->Present(1, 0);
            wc->Occluded = (hr == DXGI_STATUS_OCCLUDED);
        }

        g_frameIndex++;
    }

    WaitForPendingOperations();

    // Cleanup remaining windows
    for (WindowContext* wc : g_windows)
        DestroyWindowContext(wc);
    g_windows.clear();

    CleanupDeviceD3D();
    return 0;
}

// Helper functions

WindowContext* CreateWindowContext(const wchar_t* title, int x, int y, int width, int height, float scale, int id)
{
    WindowContext* wc = new WindowContext();
    wc->WindowId = id;

    // Register window class (once)
    static bool class_registered = false;
    if (!class_registered)
    {
        WNDCLASSEXW wc_ex = { sizeof(wc_ex), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGuiMultiWindowClass", nullptr };
        ::RegisterClassExW(&wc_ex);
        class_registered = true;
    }

    wc->Hwnd = ::CreateWindowW(L"ImGuiMultiWindowClass", title, WS_OVERLAPPEDWINDOW,
        x, y, width, height, nullptr, nullptr, GetModuleHandle(nullptr), wc);
    if (!wc->Hwnd)
    {
        delete wc;
        return nullptr;
    }

    // Create swap chain for this window
    {
        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.BufferCount = APP_NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;

        if (g_SwapChainTearingSupport)
            sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        IDXGIFactory5* dxgiFactory = nullptr;
        IDXGISwapChain1* swapChain1 = nullptr;
        if (CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)) != S_OK)
        {
            delete wc;
            return nullptr;
        }
        if (dxgiFactory->CreateSwapChainForHwnd(g_pd3dCommandQueue, wc->Hwnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
        {
            dxgiFactory->Release();
            delete wc;
            return nullptr;
        }
        if (swapChain1->QueryInterface(IID_PPV_ARGS(&wc->SwapChain)) != S_OK)
        {
            swapChain1->Release();
            dxgiFactory->Release();
            delete wc;
            return nullptr;
        }
        if (g_SwapChainTearingSupport)
            dxgiFactory->MakeWindowAssociation(wc->Hwnd, DXGI_MWA_NO_ALT_ENTER);

        swapChain1->Release();
        dxgiFactory->Release();
        wc->SwapChain->SetMaximumFrameLatency(APP_NUM_BACK_BUFFERS);
        wc->SwapChainWaitableObject = wc->SwapChain->GetFrameLatencyWaitableObject();
    }

    // Allocate RTV descriptors for this window's back buffers
    {
        SIZE_T rtvDescriptorSize = g_pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        int rtvBase = id * APP_NUM_BACK_BUFFERS;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_pd3dRtvDescHeap->GetCPUDescriptorHandleForHeapStart();
        for (int i = 0; i < APP_NUM_BACK_BUFFERS; i++)
            wc->RenderTargetDescriptor[i].ptr = rtvHandle.ptr + ((rtvBase + i) * rtvDescriptorSize);
    }

    CreateRenderTarget(wc);

    // Create ImGui context for this window
    wc->ImGuiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(wc->ImGuiCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    // Use vector font (built-in ProggyClean TTF) at larger size.
    // Setting FontSizeBase >= 15 triggers AddFontDefaultVector() automatically.
    style.FontSizeBase = 24.0f;
    io.Fonts->AddFontDefaultVector();

    ImGui_ImplWin32_Init(wc->Hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = g_pd3dDevice;
    init_info.CommandQueue = g_pd3dCommandQueue;
    init_info.NumFramesInFlight = APP_NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = g_pd3dSrvDescHeap;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
        { g_pd3dSrvDescHeapAlloc.Alloc(out_cpu, out_gpu); };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
        { g_pd3dSrvDescHeapAlloc.Free(cpu, gpu); };
    ImGui_ImplDX12_Init(&init_info);

    return wc;
}

void DestroyWindowContext(WindowContext* wc)
{
    if (!wc) return;

    if (wc->ImGuiCtx)
    {
        ImGui::SetCurrentContext(wc->ImGuiCtx);
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        wc->ImGuiCtx = nullptr;
    }

    CleanupRenderTarget(wc);

    if (wc->SwapChainWaitableObject) { CloseHandle(wc->SwapChainWaitableObject); wc->SwapChainWaitableObject = nullptr; }
    if (wc->SwapChain) { wc->SwapChain->Release(); wc->SwapChain = nullptr; }
    if (wc->Hwnd) { ::DestroyWindow(wc->Hwnd); wc->Hwnd = nullptr; }

    delete wc;
}

bool CreateDeviceD3D()
{
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&g_pd3dDevice)) != S_OK)
        return false;

#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        g_pd3dDevice->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
        pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
        const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424;
        D3D12_MESSAGE_ID disabledMessages[] = { (D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ };
        D3D12_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = 1;
        filter.DenyList.pIDList = disabledMessages;
        pInfoQueue->AddStorageFilterEntries(&filter);
        pInfoQueue->Release();
        pdx12Debug->Release();
    }
#endif

    // RTV descriptor heap: sized for all windows
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = APP_MAX_WINDOWS * APP_NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dRtvDescHeap)) != S_OK)
            return false;
    }

    // SRV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = APP_SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (g_pd3dDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
            return false;
        g_pd3dSrvDescHeapAlloc.Create(g_pd3dDevice, g_pd3dSrvDescHeap);
    }

    // Command queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (g_pd3dDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&g_pd3dCommandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (g_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&g_pd3dCommandList)) != S_OK ||
        g_pd3dCommandList->Close() != S_OK)
        return false;

    if (g_pd3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence)) != S_OK)
        return false;

    g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (g_fenceEvent == nullptr)
        return false;

    // Check tearing support
    {
        IDXGIFactory5* dxgiFactory = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) == S_OK)
        {
            BOOL allow_tearing = FALSE;
            dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
            g_SwapChainTearingSupport = (allow_tearing == TRUE);
            dxgiFactory->Release();
        }
    }

    return true;
}

void CleanupDeviceD3D()
{
    if (g_pd3dSrvDescHeap) { g_pd3dSrvDescHeap->Release(); g_pd3dSrvDescHeap = nullptr; }
    if (g_pd3dRtvDescHeap) { g_pd3dRtvDescHeap->Release(); g_pd3dRtvDescHeap = nullptr; }
    for (UINT i = 0; i < APP_NUM_FRAMES_IN_FLIGHT; i++)
        if (g_frameContext[i].CommandAllocator) { g_frameContext[i].CommandAllocator->Release(); g_frameContext[i].CommandAllocator = nullptr; }
    if (g_pd3dCommandList) { g_pd3dCommandList->Release(); g_pd3dCommandList = nullptr; }
    if (g_pd3dCommandQueue) { g_pd3dCommandQueue->Release(); g_pd3dCommandQueue = nullptr; }
    if (g_fence) { g_fence->Release(); g_fence = nullptr; }
    if (g_fenceEvent) { CloseHandle(g_fenceEvent); g_fenceEvent = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

void CreateRenderTarget(WindowContext* wc)
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        wc->SwapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, wc->RenderTargetDescriptor[i]);
        wc->RenderTargetResource[i] = pBackBuffer;
    }
}

void CleanupRenderTarget(WindowContext* wc)
{
    for (UINT i = 0; i < APP_NUM_BACK_BUFFERS; i++)
        if (wc->RenderTargetResource[i]) { wc->RenderTargetResource[i]->Release(); wc->RenderTargetResource[i] = nullptr; }
}

void WaitForPendingOperations()
{
    g_pd3dCommandQueue->Signal(g_fence, ++g_fenceLastSignaledValue);
    g_fence->SetEventOnCompletion(g_fenceLastSignaledValue, g_fenceEvent);
    ::WaitForSingleObject(g_fenceEvent, INFINITE);
}

FrameContext* WaitForNextFrameContext()
{
    FrameContext* frame_context = &g_frameContext[g_frameIndex % APP_NUM_FRAMES_IN_FLIGHT];

    // Wait for the first window's swap chain waitable object (they all run on same VSYNC)
    HANDLE hWaitable = nullptr;
    for (WindowContext* wc : g_windows)
    {
        if (wc->SwapChainWaitableObject)
        {
            hWaitable = wc->SwapChainWaitableObject;
            break;
        }
    }

    if (g_fence->GetCompletedValue() < frame_context->FenceValue)
    {
        g_fence->SetEventOnCompletion(frame_context->FenceValue, g_fenceEvent);
        if (hWaitable)
        {
            HANDLE waitableObjects[] = { hWaitable, g_fenceEvent };
            ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
        }
        else
        {
            ::WaitForSingleObject(g_fenceEvent, INFINITE);
        }
    }
    else if (hWaitable)
    {
        ::WaitForSingleObject(hWaitable, INFINITE);
    }

    return frame_context;
}

// Win32 message handler
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Retrieve the WindowContext from GWLP_USERDATA
    WindowContext* wc = (WindowContext*)::GetWindowLongPtrW(hWnd, GWLP_USERDATA);

    // Route input to the correct ImGui context
    if (wc && wc->ImGuiCtx)
    {
        ImGui::SetCurrentContext(wc->ImGuiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
    }

    switch (msg)
    {
    case WM_CREATE:
    {
        // Store WindowContext from CREATESTRUCT
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return 0;
    }
    case WM_SIZE:
        if (wc && wc->SwapChain && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget(wc);
            DXGI_SWAP_CHAIN_DESC1 desc = {};
            wc->SwapChain->GetDesc1(&desc);
            HRESULT result = wc->SwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), desc.Format, desc.Flags);
            IM_ASSERT(SUCCEEDED(result) && "Failed to resize swapchain.");
            CreateRenderTarget(wc);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        if (wc)
            wc->Closed = true;
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
