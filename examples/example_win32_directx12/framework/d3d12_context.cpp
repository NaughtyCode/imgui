#include "d3d12_context.h"
#include "imgui.h"
#include <cassert>

bool D3D12Context::Init()
{
#ifdef DX12_ENABLE_DEBUG_LAYER
    ID3D12Debug* pdx12Debug = nullptr;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&pdx12Debug))))
        pdx12Debug->EnableDebugLayer();
#endif

    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    if (D3D12CreateDevice(nullptr, featureLevel, IID_PPV_ARGS(&m_device)) != S_OK)
        return false;

#ifdef DX12_ENABLE_DEBUG_LAYER
    if (pdx12Debug != nullptr)
    {
        ID3D12InfoQueue* pInfoQueue = nullptr;
        m_device->QueryInterface(IID_PPV_ARGS(&pInfoQueue));
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

    // RTV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        desc.NumDescriptors = RTV_HEAP_MAX_WINDOWS * NUM_BACK_BUFFERS;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        desc.NodeMask = 1;
        if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_rtvDescHeap)) != S_OK)
            return false;

        m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        // Populate RTV free list: each block is 2 descriptors
        m_rtvFreeBlocks.reserve(RTV_HEAP_MAX_WINDOWS);
        for (int i = RTV_HEAP_MAX_WINDOWS - 1; i >= 0; i--)
            m_rtvFreeBlocks.push_back(i * NUM_BACK_BUFFERS);
    }

    // SRV descriptor heap
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = SRV_HEAP_SIZE;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvDescHeap)) != S_OK)
            return false;
        m_srvDescHeapAlloc.Create(m_device, m_srvDescHeap);
    }

    // Command queue
    {
        D3D12_COMMAND_QUEUE_DESC desc = {};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        desc.NodeMask = 1;
        if (m_device->CreateCommandQueue(&desc, IID_PPV_ARGS(&m_commandQueue)) != S_OK)
            return false;
    }

    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_frameContext[i].CommandAllocator)) != S_OK)
            return false;

    if (m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frameContext[0].CommandAllocator, nullptr, IID_PPV_ARGS(&m_commandList)) != S_OK ||
        m_commandList->Close() != S_OK)
        return false;

    if (m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)) != S_OK)
        return false;

    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
        return false;

    // Check tearing support
    {
        IDXGIFactory5* dxgiFactory = nullptr;
        if (CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)) == S_OK)
        {
            BOOL allow_tearing = FALSE;
            dxgiFactory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing));
            m_tearingSupport = (allow_tearing == TRUE);
            dxgiFactory->Release();
        }
    }

    return true;
}

void D3D12Context::Shutdown()
{
    if (m_srvDescHeap) { m_srvDescHeapAlloc.Destroy(); m_srvDescHeap->Release(); m_srvDescHeap = nullptr; }
    if (m_rtvDescHeap) { m_rtvDescHeap->Release(); m_rtvDescHeap = nullptr; }
    for (UINT i = 0; i < NUM_FRAMES_IN_FLIGHT; i++)
        if (m_frameContext[i].CommandAllocator) { m_frameContext[i].CommandAllocator->Release(); m_frameContext[i].CommandAllocator = nullptr; }
    if (m_commandList) { m_commandList->Release(); m_commandList = nullptr; }
    if (m_commandQueue) { m_commandQueue->Release(); m_commandQueue = nullptr; }
    if (m_fence) { m_fence->Release(); m_fence = nullptr; }
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    if (m_device) { m_device->Release(); m_device = nullptr; }

    m_rtvFreeBlocks.clear();

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1* pDebug = nullptr;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&pDebug))))
    {
        pDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_SUMMARY);
        pDebug->Release();
    }
#endif
}

bool D3D12Context::AllocRtvSlots(RtvSlot& out_slots)
{
    if (m_rtvFreeBlocks.empty())
        return false;

    int blockIndex = m_rtvFreeBlocks.back();
    m_rtvFreeBlocks.pop_back();

    D3D12_CPU_DESCRIPTOR_HANDLE heapStart = m_rtvDescHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        out_slots.Handles[i].ptr = heapStart.ptr + (static_cast<SIZE_T>(blockIndex + i)) * m_rtvDescriptorSize;
    }
    out_slots.BlockIndex = blockIndex;
    return true;
}

void D3D12Context::FreeRtvSlots(const RtvSlot& slots)
{
    if (slots.BlockIndex >= 0)
        m_rtvFreeBlocks.push_back(slots.BlockIndex);
}

FrameContext* D3D12Context::WaitForNextFrameContext(HANDLE hWaitable)
{
    FrameContext* frameCtx = &m_frameContext[m_frameIndex % NUM_FRAMES_IN_FLIGHT];

    if (m_fence->GetCompletedValue() < frameCtx->FenceValue)
    {
        m_fence->SetEventOnCompletion(frameCtx->FenceValue, m_fenceEvent);
        if (hWaitable)
        {
            HANDLE waitableObjects[] = { hWaitable, m_fenceEvent };
            ::WaitForMultipleObjects(2, waitableObjects, TRUE, INFINITE);
        }
        else
        {
            ::WaitForSingleObject(m_fenceEvent, INFINITE);
        }
    }
    else if (hWaitable)
    {
        ::WaitForSingleObject(hWaitable, INFINITE);
    }

    return frameCtx;
}

void D3D12Context::SignalFrame(FrameContext* frameCtx)
{
    m_fenceLastSignaledValue++;
    m_commandQueue->Signal(m_fence, m_fenceLastSignaledValue);
    frameCtx->FenceValue = m_fenceLastSignaledValue;
}

void D3D12Context::WaitForPendingOperations()
{
    m_fenceLastSignaledValue++;
    m_commandQueue->Signal(m_fence, m_fenceLastSignaledValue);
    m_fence->SetEventOnCompletion(m_fenceLastSignaledValue, m_fenceEvent);
    ::WaitForSingleObject(m_fenceEvent, INFINITE);
}
