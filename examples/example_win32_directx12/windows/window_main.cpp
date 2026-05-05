#include "window_main.h"
#include "imgui.h"

void WindowMain::Render()
{
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Main Control Panel");

    ImGui::Text("Welcome to the multi-window demo!");
    ImGui::Separator();

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / io.Framerate, io.Framerate);

    ImGui::Spacing();
    ImGui::Text("Active Windows:");
    ImGui::BulletText("Window 1 - Demo (borderless)");
    ImGui::BulletText("Window 2 - Plots (borderless)");
    ImGui::BulletText("Window 3 - Settings (borderless)");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("This main window has standard system decorations.");
    ImGui::Text("Other windows are borderless with custom title bars.");

    static float color[3] = { 0.45f, 0.55f, 0.60f };
    ImGui::ColorEdit3("Background", color);
    m_clearColor = ImVec4(color[0], color[1], color[2], 1.0f);

    ImGui::End();
}

LRESULT WindowMain::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_CLOSE)
        ::PostQuitMessage(0);
    return WindowBase::HandleMessage(msg, wParam, lParam);
}
