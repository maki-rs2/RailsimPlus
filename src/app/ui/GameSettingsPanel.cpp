//
// GameSettingsPanel.cpp - 「ゲーム設定」パネル描画
//
// 言語・音量・プラグインルート(一覧と追加)・レイアウト保存先・fps・カメラを原点へ戻す操作・時刻・太陽位置(自転)を置く。
// 描画距離・空と環境光・トーンマップ・シャドウ品質は「描画設定」(RenderSettingsPanel.cpp)にある。
//
#include "UiPanels.h"

#include <filesystem>
#include <iterator>
#include <optional>
#include <string>

#include "imgui.h"

#include "../settings/UserSettings.h"
#include "../../platform/text/Encoding.h"
#include "../../world/time/SolarModeNames.h"

namespace rs::app {

void DrawGameSettingsPanel(const GameSettingsPanelContext& ctx) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Begin("ゲーム設定");

    const GameSettingsViewModel viewModel = ctx.action.ViewModel();
    int languageIndex = viewModel.language == Language::Japanese ? 0 : 1;
    static const char* kLanguageNames[] = {"日本語", "English"};
    if (ImGui::Combo("言語 / Language", &languageIndex, kLanguageNames, IM_ARRAYSIZE(kLanguageNames))) {
        const Language language = languageIndex == 0 ? Language::Japanese : Language::English;
        ctx.action.SetLanguage(language);
    }
    ImGui::Separator();

    ImGui::Text("オーディオ出力デバイス:");
    {
        float masterVolumePercent = viewModel.audioMasterVolume * UserSettingsData::kVolumePercentScale;
        float trainVolumePercent = viewModel.audioTrainVolume * UserSettingsData::kVolumePercentScale;
        float environmentVolumePercent = viewModel.audioEnvironmentVolume * UserSettingsData::kVolumePercentScale;
        bool volumeCommit = false;
        if (ImGui::SliderFloat("全体", &masterVolumePercent, UserSettingsData::kMinAudioVolumePercent,
                               UserSettingsData::kMaxAudioVolumePercent, "%.0f %%")) {
            ctx.action.SetAudioMasterVolume(masterVolumePercent / UserSettingsData::kVolumePercentScale);
        }
        volumeCommit = ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::SliderFloat("列車", &trainVolumePercent, UserSettingsData::kMinAudioVolumePercent,
                               UserSettingsData::kMaxAudioVolumePercent, "%.0f %%")) {
            ctx.action.SetAudioTrainVolume(trainVolumePercent / UserSettingsData::kVolumePercentScale);
        }
        volumeCommit = ImGui::IsItemDeactivatedAfterEdit() || volumeCommit;
        if (ImGui::SliderFloat("環境", &environmentVolumePercent, UserSettingsData::kMinAudioVolumePercent,
                               UserSettingsData::kMaxAudioVolumePercent, "%.0f %%")) {
            ctx.action.SetAudioEnvironmentVolume(environmentVolumePercent / UserSettingsData::kVolumePercentScale);
        }
        volumeCommit = ImGui::IsItemDeactivatedAfterEdit() || volumeCommit;
        if (volumeCommit) ctx.action.CommitAudioCategoryVolumes();

        if (!ctx.state.audioOutputDevicesFetched) {
            ctx.state.cachedAudioOutputDevices = ctx.action.EnumerateAudioOutputDevices();
            ctx.state.audioOutputDevicesFetched = true;
        }
        const std::string& current = viewModel.audioOutputDevice;
        const std::string previewLabel = current.empty() ? "システム既定" : current;
        if (ImGui::BeginCombo("出力デバイス", previewLabel.c_str())) {
            if (ImGui::Selectable("システム既定", current.empty())) {
                ctx.action.SetAudioOutputDevice("");
            }
            for (const std::string& deviceName : ctx.state.cachedAudioOutputDevices) {
                if (ImGui::Selectable(deviceName.c_str(), deviceName == current)) {
                    ctx.action.SetAudioOutputDevice(deviceName);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("更新##audioDevices")) {
            ctx.state.cachedAudioOutputDevices = ctx.action.EnumerateAudioOutputDevices();
        }
    }
    ImGui::Separator();

    // プラグインルートは複数登録でき、並べ替え・削除はRmlUi側の画面で行う。ここは一覧の表示と追加だけ。
    ImGui::TextUnformatted("プラグインルート");
    for (const std::string& root : viewModel.pluginRoots) {
        ImGui::BulletText("%s", root.c_str());
    }
    if (viewModel.pluginRoots.empty()) {
        const std::string executableDirectoryUtf8 =
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
        ImGui::TextDisabled("既定: %s", executableDirectoryUtf8.c_str());
    }
    if (const std::optional<std::string> picked =
            ctx.pathPicker.Button("ルートを追加…##pluginRoot", FolderPick("プラグインルートを選択"))) {
        ctx.action.AddPluginRoot(*picked);
    }

    if (const std::optional<std::string> picked =
            ctx.pathPicker.Field("レイアウト保存先##layoutSaveDirectory", viewModel.layoutSaveDirectory,
                                 FolderPick("レイアウトの保存先を選択"))) {
        ctx.action.SetLayoutSaveDirectory(*picked);
    }
    if (viewModel.layoutSaveDirectory.empty()) {
        const std::filesystem::path defaultDirectory = UserSettings::DefaultLayoutSaveDirectory();
        const std::string defaultDirectoryUtf8 = rs::platform_text::WideToUtf8(defaultDirectory.wstring());
        ImGui::TextDisabled("既定: %s", defaultDirectoryUtf8.c_str());
    } else {
        // 直打ちで空にして既定へ戻せなくなったので、戻す操作を別に置く。
        ImGui::SameLine();
        if (ImGui::Button("既定に戻す##layoutSaveDirectory")) ctx.action.SetLayoutSaveDirectory("");
    }
    ImGui::Separator();

    ImGui::Text("%.1f fps (%.2f ms)", io.Framerate,
                io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);


    ImGui::Separator();
    ImGui::Text("カメラ:");
    ImGui::SameLine();
    if (ImGui::Button("原点へ")) {
        ctx.action.ResetCamera(); // カメラを初期状態(原点注視)へ戻す
    }

    ImGui::Separator();
    ImGui::Text("時刻:");
    {
        static const char* kWeekdayNames[7] = {"日", "月", "火", "水", "木", "金", "土"};
        ImGui::Text("%04d-%02d-%02d(%s) %02d:%02d:%02d", viewModel.year, viewModel.month,
                    viewModel.day, kWeekdayNames[viewModel.dayOfWeek], viewModel.hour,
                    viewModel.minute, viewModel.second);

        float timeScale = viewModel.timeScale;
        if (ImGui::DragFloat("TimeScale", &timeScale, 1.0f,
                             static_cast<float>(rs::world::GameClock::kMinTimeScale),
                             static_cast<float>(rs::world::GameClock::kMaxTimeScale), "%.1f")) {
            ctx.action.SetTimeScale(timeScale);
        }
    }

    ImGui::Separator();
    ImGui::Text("太陽位置:");
    {
        static const char* kModeNames[] = {"時刻連動", "常に昼", "常に夜"};
        static_assert(std::size(kModeNames) == rs::world::kEarthRotationKeys.size());
        int mode = viewModel.earthRotationMode;
        if (ImGui::Combo("自転", &mode, kModeNames, static_cast<int>(std::size(kModeNames)))) {
            ctx.action.SetEarthRotationMode(mode);
        }
    }

    ImGui::End();
}

} // namespace rs::app
