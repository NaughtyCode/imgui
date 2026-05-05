#include "window_settings.h"

void WindowSettings::Render()
{
    static bool vsync = true;
    static bool fullscreen = false;
    static int aa_samples = 4;
    static float volume = 0.75f;
    static int resolution = 0;
    const char* resolutions[] = { "1920x1080", "2560x1440", "3840x2160" };

    ImGui::Begin("Window 3 - Settings");
    ImGui::Text("Settings and Controls");

    ImGui::Checkbox("VSync", &vsync);
    ImGui::Checkbox("Fullscreen", &fullscreen);
    ImGui::SliderFloat("Volume", &volume, 0.0f, 1.0f);
    ImGui::Combo("Resolution", &resolution, resolutions, 3);
    ImGui::RadioButton("Low", &aa_samples, 0); ImGui::SameLine();
    ImGui::RadioButton("Medium", &aa_samples, 2); ImGui::SameLine();
    ImGui::RadioButton("High", &aa_samples, 4);

    ImGui::Separator();
    ImGui::ColorEdit3("Background", reinterpret_cast<float*>(&m_clearColor));

    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("WantCaptureMouse: %s", io.WantCaptureMouse ? "true" : "false");
    ImGui::Text("WantCaptureKeyboard: %s", io.WantCaptureKeyboard ? "true" : "false");
    ImGui::End();
}
