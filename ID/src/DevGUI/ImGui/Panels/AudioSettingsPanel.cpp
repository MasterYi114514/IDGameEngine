#include "DevGUI/ImGui/Panels/AudioSettingsPanel.hpp"

#include "IDAudio.hpp"
#include "Log/Log.hpp"

namespace ID
{
    AudioSettingsPanel::AudioSettingsPanel() : ImGuiPanel("Audio Settings", true) { }

    void AudioSettingsPanel::on_imgui_render()
    {
        if(!begin_window()) return;

        // ---- Master Volume ----
        float volume = AudioEngine::get_master_volume();
        if(ImGui::SliderFloat("Master Volume", &volume, 0.0f, 1.0f, "%.2f"))
        {
            AudioEngine::set_master_volume(volume);
            ID_INFO("[AudioSettings] 主音量已设置为 {:.2f}", volume);
        }

        ImGui::Separator();

        // ---- Listener（只读展示）----
        ImGui::Text("Listener:");
        ImGui::TextDisabled("位置/朝向由 AudioListenerComponent 每帧");
        ImGui::TextDisabled("从 TransformComponent 同步（通常挂载于相机）");
        ImGui::TextDisabled("引擎暂未提供监听器状态查询接口");

        end_window();
    }
} // namespace ID
