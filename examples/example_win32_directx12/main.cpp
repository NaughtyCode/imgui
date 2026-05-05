// Dear ImGui: standalone example application for Windows API + DirectX 12
// Multi-window version: multiple native Win32 windows, each with its own ImGui context.
// Refactored with C++17: abstract WindowBase + polymorphic Render() per window type.

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "framework/window_manager.h"
#include "windows/window_main.h"
#include "windows/window_demo.h"
#include "windows/window_plots.h"
#include "windows/window_settings.h"

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

    auto* wcMain = manager.AddWindow<WindowMain>(
        L"Main Window", 50, 50, 1000, 700, main_scale, true);
    auto* wc1 = manager.AddWindow<WindowDemo>(
        L"Window 1 - Demo", 100, 100, 960, 600, main_scale, false);
    auto* wc2 = manager.AddWindow<WindowPlots>(
        L"Window 2 - Plots", 120, 140, 640, 480, main_scale, false);
    auto* wc3 = manager.AddWindow<WindowSettings>(
        L"Window 3 - Settings", 140, 180, 560, 420, main_scale, false);

    if (!wcMain || !wc1 || !wc2 || !wc3)
        return 1;

    wcMain->GetClearColor() = ImVec4(0.35f, 0.40f, 0.50f, 1.00f);
    wc1->GetClearColor() = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    wc2->GetClearColor() = ImVec4(0.30f, 0.45f, 0.30f, 1.00f);
    wc3->GetClearColor() = ImVec4(0.40f, 0.35f, 0.55f, 1.00f);

    return manager.Run();
}
