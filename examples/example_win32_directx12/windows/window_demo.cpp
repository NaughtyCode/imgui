#include "window_demo.h"

void WindowDemo::Render()
{
    DrawCustomTitleBar("Window 1 - Demo");

    static bool showDemoWindow = true;
    static bool showAnotherWindow = true;
    static float f = 0.0f;
    static int counter = 0;

    if (showDemoWindow)
        ImGui::ShowDemoWindow(&showDemoWindow);

    ImGuiIO& io = ImGui::GetIO();

    ImGui::Begin("Hello, world!");
    ImGui::Text("This is window 1 content.");
    ImGui::Checkbox("Demo Window", &showDemoWindow);
    ImGui::Checkbox("Another Window", &showAnotherWindow);
    ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
    ImGui::ColorEdit3("clear color", reinterpret_cast<float*>(&m_clearColor));
    if (ImGui::Button("Button"))
        counter++;
    ImGui::SameLine();
    ImGui::Text("counter = %d", counter);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
    ImGui::End();

    if (showAnotherWindow)
    {
        ImGui::Begin("Another Window", &showAnotherWindow);
        ImGui::Text("Hello from window 1's extra window!");
        if (ImGui::Button("Close Me"))
            showAnotherWindow = false;
        ImGui::End();
    }
}
