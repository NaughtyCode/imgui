#include "window_base.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include <cassert>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool WindowBase::s_classRegistered = false;

WindowBase::WindowBase(D3D12Context& d3d, const wchar_t* title, int x, int y, int w, int h, float scale)
    : m_d3d(d3d), m_title(title), m_x(x), m_y(y), m_width(w), m_height(h), m_scale(scale)
{
}

WindowBase::~WindowBase()
{
    Shutdown();
}

bool WindowBase::Init()
{
    if (!CreateSwapChain())
        return false;

    CreateRenderTargets();

    if (!CreateImGuiContext())
        return false;

    return true;
}

void WindowBase::Shutdown()
{
    if (m_imguiCtx)
    {
        DestroyImGuiContext();
    }

    CleanupRenderTargets();
    CleanupSwapChain();

    if (m_hwnd)
    {
        ::DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ---------- Swap Chain ----------

bool WindowBase::CreateSwapChain()
{
    if (!s_classRegistered)
    {
        WNDCLASSEXW wc_ex = {
            sizeof(wc_ex), CS_CLASSDC, StaticWndProc, 0L, 0L,
            GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
            CLASS_NAME, nullptr
        };
        ::RegisterClassExW(&wc_ex);
        s_classRegistered = true;
    }

    m_hwnd = ::CreateWindowW(CLASS_NAME, m_title.c_str(), WS_OVERLAPPEDWINDOW,
        m_x, m_y, m_width, m_height, nullptr, nullptr, GetModuleHandle(nullptr), this);
    if (!m_hwnd)
        return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.BufferCount = D3D12Context::NUM_BACK_BUFFERS;
    sd.Width = 0;
    sd.Height = 0;
    sd.Format = SWAP_CHAIN_FORMAT;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.Stereo = FALSE;

    if (m_d3d.GetTearingSupport())
        sd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    IDXGIFactory5* dxgiFactory = nullptr;
    IDXGISwapChain1* swapChain1 = nullptr;
    if (CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgiFactory)) != S_OK)
        return false;
    if (dxgiFactory->CreateSwapChainForHwnd(m_d3d.GetCommandQueue(), m_hwnd, &sd, nullptr, nullptr, &swapChain1) != S_OK)
    {
        dxgiFactory->Release();
        return false;
    }
    if (swapChain1->QueryInterface(IID_PPV_ARGS(&m_swapChain)) != S_OK)
    {
        swapChain1->Release();
        dxgiFactory->Release();
        return false;
    }
    if (m_d3d.GetTearingSupport())
        dxgiFactory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    swapChain1->Release();
    dxgiFactory->Release();
    m_swapChain->SetMaximumFrameLatency(D3D12Context::NUM_BACK_BUFFERS);
    m_swapChainWaitableObject = m_swapChain->GetFrameLatencyWaitableObject();

    // Allocate RTV slots from shared heap
    if (!m_d3d.AllocRtvSlots(m_rtvSlots))
        return false;

    return true;
}

void WindowBase::CleanupSwapChain()
{
    m_d3d.FreeRtvSlots(m_rtvSlots);
    m_rtvSlots = {};

    if (m_swapChainWaitableObject) { CloseHandle(m_swapChainWaitableObject); m_swapChainWaitableObject = nullptr; }
    if (m_swapChain) { m_swapChain->Release(); m_swapChain = nullptr; }
}

// ---------- Render Targets ----------

void WindowBase::CreateRenderTargets()
{
    for (UINT i = 0; i < D3D12Context::NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource* pBackBuffer = nullptr;
        m_swapChain->GetBuffer(i, IID_PPV_ARGS(&pBackBuffer));
        m_d3d.GetDevice()->CreateRenderTargetView(pBackBuffer, nullptr, m_rtvSlots.Handles[i]);
        m_renderTargetResource[i] = pBackBuffer;
    }
}

void WindowBase::CleanupRenderTargets()
{
    for (UINT i = 0; i < D3D12Context::NUM_BACK_BUFFERS; i++)
        if (m_renderTargetResource[i]) { m_renderTargetResource[i]->Release(); m_renderTargetResource[i] = nullptr; }
}

void WindowBase::ResizeSwapChain(UINT width, UINT height)
{
    CleanupRenderTargets();
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    m_swapChain->GetDesc1(&desc);
    HRESULT result = m_swapChain->ResizeBuffers(0, width, height, desc.Format, desc.Flags);
    IM_ASSERT(SUCCEEDED(result) && "Failed to resize swapchain.");
    CreateRenderTargets();
}

// ---------- ImGui Context ----------

bool WindowBase::CreateImGuiContext()
{
    m_imguiCtx = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_imguiCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(m_scale);
    style.FontScaleDpi = m_scale;

    // Use vector font (built-in ProggyClean TTF) at larger size
    style.FontSizeBase = 24.0f;
    io.Fonts->AddFontDefaultVector();

    ImGui_ImplWin32_Init(m_hwnd);

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = m_d3d.GetDevice();
    init_info.CommandQueue = m_d3d.GetCommandQueue();
    init_info.NumFramesInFlight = D3D12Context::NUM_FRAMES_IN_FLIGHT;
    init_info.RTVFormat = SWAP_CHAIN_FORMAT;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.SrvDescriptorHeap = m_d3d.GetSrvDescHeap();
    init_info.UserData = this;
    init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
        auto* self = static_cast<WindowBase*>(info->UserData);
        self->m_d3d.GetSrvDescHeapAlloc().Alloc(out_cpu, out_gpu);
    };
    init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu) {
        auto* self = static_cast<WindowBase*>(info->UserData);
        self->m_d3d.GetSrvDescHeapAlloc().Free(cpu, gpu);
    };
    ImGui_ImplDX12_Init(&init_info);

    return true;
}

void WindowBase::DestroyImGuiContext()
{
    ImGui::SetCurrentContext(m_imguiCtx);
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_imguiCtx = nullptr;
}

// ---------- Frame ----------

void WindowBase::BeginFrame()
{
    ImGui::SetCurrentContext(m_imguiCtx);
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void WindowBase::EndFrame()
{
    ImGui::Render();
}

ImDrawData* WindowBase::GetDrawData() const
{
    return ImGui::GetDrawData();
}

// ---------- Render Target Ops ----------

void WindowBase::PrepareRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    UINT backBufferIdx = GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_renderTargetResource[backBufferIdx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cmdList->ResourceBarrier(1, &barrier);
}

void WindowBase::ClearRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    UINT backBufferIdx = GetCurrentBackBufferIndex();
    const float clear_color_with_alpha[4] = {
        m_clearColor.x * m_clearColor.w,
        m_clearColor.y * m_clearColor.w,
        m_clearColor.z * m_clearColor.w,
        m_clearColor.w
    };
    cmdList->ClearRenderTargetView(m_rtvSlots.Handles[backBufferIdx], clear_color_with_alpha, 0, nullptr);
}

void WindowBase::FinalizeRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    UINT backBufferIdx = GetCurrentBackBufferIndex();
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = m_renderTargetResource[backBufferIdx];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cmdList->ResourceBarrier(1, &barrier);
}

HRESULT WindowBase::Present()
{
    return m_swapChain->Present(1, 0);
}

// ---------- Message Handling ----------

LRESULT WINAPI WindowBase::StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // WM_NCCREATE is the first message sent to a window. Set GWLP_USERDATA here
    // so that all subsequent messages (including WM_CREATE) can find the WindowBase.
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    WindowBase* self = reinterpret_cast<WindowBase*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    if (self && self->m_imguiCtx)
    {
        ImGui::SetCurrentContext(self->m_imguiCtx);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);

    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WindowBase::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
        if (m_swapChain && wParam != SIZE_MINIMIZED)
            ResizeSwapChain(static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)));
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_CLOSE:
        m_closed = true;
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
