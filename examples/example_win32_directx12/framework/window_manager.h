#pragma once
#include "d3d12_context.h"
#include "window_base.h"
#include <memory>
#include <vector>

class WindowManager
{
public:
    WindowManager() = default;

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    bool Init();

    template<typename T, typename... Args>
    T* AddWindow(Args&&... args)
    {
        auto w = std::make_unique<T>(m_d3d, std::forward<Args>(args)...);
        T* ptr = w.get();
        if (!w->Init())
            return nullptr;
        ::ShowWindow(ptr->GetHwnd(), SW_SHOWDEFAULT);
        ::UpdateWindow(ptr->GetHwnd());
        m_windows.push_back(std::move(w));
        return ptr;
    }

    int Run();

private:
    std::vector<std::unique_ptr<WindowBase>> m_windows;
    D3D12Context                             m_d3d;
};
