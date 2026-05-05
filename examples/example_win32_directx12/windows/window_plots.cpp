#include "window_plots.h"
#include <cmath>

void WindowPlots::Render()
{
    static bool showAnotherWindow = true;
    static float values[90] = {};
    static int values_offset = 0;
    static float refresh_time = 0.0f;

    ImGui::Begin("Window 2 - Plots");
    ImGui::Text("This window demonstrates plot widgets.");

    ImGuiIO& io = ImGui::GetIO();
    if (refresh_time == 0.0f)
        refresh_time = static_cast<float>(ImGui::GetTime());
    while (refresh_time < ImGui::GetTime())
    {
        values[values_offset] = sinf(refresh_time * 3.0f) * 0.5f + 0.5f;
        values_offset = (values_offset + 1) % 90;
        refresh_time += 1.0f / 60.0f;
    }

    ImGui::PlotLines("Sine Wave", values, 90, values_offset, nullptr, 0.0f, 1.0f, ImVec2(0, 120));
    ImGui::PlotHistogram("Histogram", values, 90, values_offset, nullptr, 0.0f, 1.0f, ImVec2(0, 120));
    ImGui::Text("FPS: %.1f", io.Framerate);

    static int slider_val = 50;
    ImGui::SliderInt("Value", &slider_val, 0, 100);
    ImGui::ProgressBar(slider_val / 100.0f);
    ImGui::End();

    if (showAnotherWindow)
    {
        ImGui::Begin("Window 2 - Extra", &showAnotherWindow);
        ImGui::Text("Another window in window 2!");
        static bool checked = true;
        ImGui::Checkbox("Check me", &checked);
        ImGui::End();
    }
}
