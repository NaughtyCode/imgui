// Dear ImGui: standalone example application for Windows API + DirectX 12
// Multi-window version: multiple native Win32 windows, each with its own ImGui context.
// Refactored with C++17: abstract WindowBase + polymorphic Render() per window type.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "window_manager.h"
#include "window_demo.h"
#include "window_plots.h"
#include "window_settings.h"

// Required by ImGui_ImplWin32_WndProcHandler (declared in window_base.cpp as well)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

    WindowManager manager;
    if (!manager.Init())
        return 1;

    auto* wc1 = manager.AddWindow<WindowDemo>(
        L"ImGui Window 1 - Demo", 100, 100, 1280, 800, main_scale);
    auto* wc2 = manager.AddWindow<WindowPlots>(
        L"ImGui Window 2 - Plots", 150, 180, 800, 600, main_scale);
    auto* wc3 = manager.AddWindow<WindowSettings>(
        L"ImGui Window 3 - Settings", 200, 260, 700, 500, main_scale);

    if (!wc1 || !wc2 || !wc3)
        return 1;

    wc1->GetClearColor() = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    wc2->GetClearColor() = ImVec4(0.30f, 0.45f, 0.30f, 1.00f);
    wc3->GetClearColor() = ImVec4(0.40f, 0.35f, 0.55f, 1.00f);

    return manager.Run();
}
