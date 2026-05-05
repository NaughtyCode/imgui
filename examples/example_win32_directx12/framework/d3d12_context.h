#pragma once
#include "imgui.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <vector>

#ifdef _DEBUG
#define DX12_ENABLE_DEBUG_LAYER
#endif

#ifdef DX12_ENABLE_DEBUG_LAYER
#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
#endif

struct FrameContext
{
    ID3D12CommandAllocator* CommandAllocator = nullptr;
    UINT64                  FenceValue = 0;
};

struct ExampleDescriptorHeapAllocator
{
    ID3D12DescriptorHeap*       Heap = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE  HeapType = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
    D3D12_CPU_DESCRIPTOR_HANDLE HeapStartCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE HeapStartGpu = {};
    UINT                        HeapHandleIncrement = 0;
    std::vector<int>            FreeIndices;

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
    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu)
    {
        IM_ASSERT(!FreeIndices.empty());
        int idx = FreeIndices.back();
        FreeIndices.pop_back();
        out_cpu->ptr = HeapStartCpu.ptr + (idx * HeapHandleIncrement);
        out_gpu->ptr = HeapStartGpu.ptr + (idx * HeapHandleIncrement);
    }
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu)
    {
        int cpu_idx = (int)((cpu.ptr - HeapStartCpu.ptr) / HeapHandleIncrement);
        int gpu_idx = (int)((gpu.ptr - HeapStartGpu.ptr) / HeapHandleIncrement);
        IM_ASSERT(cpu_idx == gpu_idx);
        FreeIndices.push_back(cpu_idx);
    }
};

class D3D12Context
{
public:
    static constexpr int NUM_FRAMES_IN_FLIGHT = 2;
    static constexpr int NUM_BACK_BUFFERS     = 2;
    static constexpr int SRV_HEAP_SIZE        = 64;
    static constexpr int RTV_HEAP_MAX_WINDOWS = 8;

    struct RtvSlot
    {
        D3D12_CPU_DESCRIPTOR_HANDLE Handles[NUM_BACK_BUFFERS] = {};
        int BlockIndex = -1;
    };

    D3D12Context() = default;
    ~D3D12Context() { Shutdown(); }

    D3D12Context(const D3D12Context&) = delete;
    D3D12Context& operator=(const D3D12Context&) = delete;
    D3D12Context(D3D12Context&&) = delete;
    D3D12Context& operator=(D3D12Context&&) = delete;

    bool Init();
    void Shutdown();

    FrameContext* WaitForNextFrameContext(HANDLE hWaitable);
    void SignalFrame(FrameContext* frameCtx);
    void WaitForPendingOperations();
    void IncrementFrameIndex() { m_frameIndex++; }

    bool AllocRtvSlots(RtvSlot& out_slots);
    void FreeRtvSlots(const RtvSlot& slots);

    ID3D12Device*              GetDevice() const           { return m_device; }
    ID3D12CommandQueue*        GetCommandQueue() const     { return m_commandQueue; }
    ID3D12GraphicsCommandList* GetCommandList() const      { return m_commandList; }
    ID3D12Fence*               GetFence() const            { return m_fence; }
    ID3D12DescriptorHeap*      GetSrvDescHeap() const      { return m_srvDescHeap; }
    HANDLE                     GetFenceEvent() const       { return m_fenceEvent; }
    bool                       GetTearingSupport() const   { return m_tearingSupport; }
    ExampleDescriptorHeapAllocator& GetSrvDescHeapAlloc()  { return m_srvDescHeapAlloc; }

private:
    ID3D12Device*              m_device                 = nullptr;
    ID3D12CommandQueue*        m_commandQueue           = nullptr;
    ID3D12GraphicsCommandList* m_commandList            = nullptr;
    ID3D12Fence*               m_fence                  = nullptr;
    HANDLE                     m_fenceEvent             = nullptr;
    UINT64                     m_fenceLastSignaledValue = 0;

    ID3D12DescriptorHeap*      m_rtvDescHeap            = nullptr;
    ID3D12DescriptorHeap*      m_srvDescHeap            = nullptr;
    ExampleDescriptorHeapAllocator m_srvDescHeapAlloc;

    FrameContext               m_frameContext[NUM_FRAMES_IN_FLIGHT] = {};
    UINT                       m_frameIndex = 0;

    bool                       m_tearingSupport = false;

    std::vector<int>           m_rtvFreeBlocks;
    SIZE_T                     m_rtvDescriptorSize = 0;
};
