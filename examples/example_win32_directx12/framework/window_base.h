#pragma once
#include "d3d12_context.h"
#include "imgui.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <windows.h>
#include <string>

class WindowBase
{
public:
    WindowBase(D3D12Context& d3d, const wchar_t* title, int x, int y, int w, int h, float scale, bool bordered = true);
    virtual ~WindowBase();

    WindowBase(const WindowBase&) = delete;
    WindowBase& operator=(const WindowBase&) = delete;
    WindowBase(WindowBase&&) = delete;
    WindowBase& operator=(WindowBase&&) = delete;

    bool Init();
    void Shutdown();

    // Frame building
    void BeginFrame();
    void EndFrame();

    // Override in derived classes for custom UI
    virtual void Render() = 0;

    // D3D12 render target helpers (called during composite render pass)
    void PrepareRenderTarget(ID3D12GraphicsCommandList* cmdList);
    void ClearRenderTarget(ID3D12GraphicsCommandList* cmdList);
    void FinalizeRenderTarget(ID3D12GraphicsCommandList* cmdList);

    HRESULT Present();

    // Accessors
    HWND                         GetHwnd() const                   { return m_hwnd; }
    ImGuiContext*                GetImGuiContext() const           { return m_imguiCtx; }
    ImDrawData*                  GetDrawData() const;
    IDXGISwapChain3*             GetSwapChain() const              { return m_swapChain; }
    HANDLE                       GetSwapChainWaitableObject() const{ return m_swapChainWaitableObject; }
    D3D12_CPU_DESCRIPTOR_HANDLE  GetRenderTargetDescriptor(int idx) const { return m_rtvSlots.Handles[idx]; }
    ID3D12Resource*              GetRenderTargetResource(int idx) const    { return m_renderTargetResource[idx]; }
    UINT                         GetCurrentBackBufferIndex() const { return m_swapChain->GetCurrentBackBufferIndex(); }
    bool                         IsClosed() const                  { return m_closed; }
    bool                         IsOccluded() const                { return m_occluded; }
    float                        GetScale() const                  { return m_scale; }

    void SetOccluded(bool v) { m_occluded = v; }
    void SetClosed(bool v)   { m_closed = v; }

    ImVec4& GetClearColor() { return m_clearColor; }

protected:
    virtual LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Custom title bar for borderless windows
    void DrawCustomTitleBar(const char* title);
    void Minimize();
    void Maximize();
    void Restore();

    D3D12Context& m_d3d;
    HWND          m_hwnd = nullptr;
    ImVec4        m_clearColor = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    float         m_scale = 1.0f;
    bool          m_bordered = true;
    float         m_titleBarHeight = 32.0f;

private:
    static LRESULT WINAPI StaticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateSwapChain();
    void CleanupSwapChain();
    bool CreateImGuiContext();
    void DestroyImGuiContext();
    void CreateRenderTargets();
    void CleanupRenderTargets();
    void ResizeSwapChain(UINT width, UINT height);

    static bool                 s_classRegistered;
    static constexpr const wchar_t* CLASS_NAME = L"ImGuiMultiWindowClass";
    static constexpr DXGI_FORMAT SWAP_CHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

    std::wstring   m_title;
    int            m_x = 0, m_y = 0, m_width = 0, m_height = 0;

    IDXGISwapChain3*       m_swapChain = nullptr;
    HANDLE                 m_swapChainWaitableObject = nullptr;

    ID3D12Resource*        m_renderTargetResource[D3D12Context::NUM_BACK_BUFFERS] = {};
    D3D12Context::RtvSlot  m_rtvSlots = {};

    ImGuiContext*          m_imguiCtx = nullptr;

    bool m_closed   = false;
    bool m_occluded = false;
};
