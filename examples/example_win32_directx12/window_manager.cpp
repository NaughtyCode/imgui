#include "window_manager.h"
#include "imgui_impl_dx12.h"

bool WindowManager::Init()
{
    return m_d3d.Init();
}

int WindowManager::Run()
{
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
        m_windows.erase(
            std::remove_if(m_windows.begin(), m_windows.end(),
                [](const std::unique_ptr<WindowBase>& w) { return w->IsClosed(); }),
            m_windows.end());

        if (m_windows.empty())
            break;

        // Check if all windows are occluded
        bool all_occluded = true;
        for (auto& w : m_windows)
        {
            if (!w->IsOccluded() && !::IsIconic(w->GetHwnd()))
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

        // Build UI for each window
        for (auto& w : m_windows)
        {
            w->BeginFrame();
            w->Render();
            w->EndFrame();
        }

        // Composite rendering: single command list for all windows
        HANDLE hFirstWaitable = nullptr;
        for (auto& w : m_windows)
        {
            if (w->GetSwapChainWaitableObject())
            {
                hFirstWaitable = w->GetSwapChainWaitableObject();
                break;
            }
        }

        FrameContext* frameCtx = m_d3d.WaitForNextFrameContext(hFirstWaitable);
        frameCtx->CommandAllocator->Reset();

        ID3D12GraphicsCommandList* cmdList = m_d3d.GetCommandList();
        cmdList->Reset(frameCtx->CommandAllocator, nullptr);

        ID3D12DescriptorHeap* heaps[] = { m_d3d.GetSrvDescHeap() };
        cmdList->SetDescriptorHeaps(1, heaps);

        for (auto& w : m_windows)
        {
            if (w->IsOccluded() || ::IsIconic(w->GetHwnd()))
                continue;

            w->PrepareRenderTarget(cmdList);
            w->ClearRenderTarget(cmdList);

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = w->GetRenderTargetDescriptor(
                w->GetCurrentBackBufferIndex());
            cmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

            ImGui::SetCurrentContext(w->GetImGuiContext());
            ImGui_ImplDX12_RenderDrawData(w->GetDrawData(), cmdList);

            w->FinalizeRenderTarget(cmdList);
        }

        cmdList->Close();
        ID3D12CommandList* const cmdLists[] = { cmdList };
        m_d3d.GetCommandQueue()->ExecuteCommandLists(1, cmdLists);
        m_d3d.SignalFrame(frameCtx);

        // Present all visible windows
        for (auto& w : m_windows)
        {
            if (w->IsOccluded() || ::IsIconic(w->GetHwnd()))
                continue;
            HRESULT hr = w->Present();
            w->SetOccluded(hr == DXGI_STATUS_OCCLUDED);
        }

        m_d3d.IncrementFrameIndex();
    }

    m_d3d.WaitForPendingOperations();

    // Destroy windows before D3DContext shuts down
    m_windows.clear();

    return 0;
}
