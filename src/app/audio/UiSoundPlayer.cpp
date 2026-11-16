#include "UiSoundPlayer.h"

#include <array>
#include <string>
#include <string_view>

#include "../../platform/file/AssetPath.h"
#include "../../platform/text/Encoding.h"
#include "../../sim/audio/AudioEngine.h"

namespace rs::app {

namespace {

struct UiSoundFile {
    UiSound sound;
    const char* id;
    const char* path;
};

// UiSoundの並びと同じ順(UiSoundPlayer.hの列挙)。TrainRemoveとDeleteは同じidと同じファイルを共有する。
constexpr UiSoundFile kUiSoundFiles[] = {
    {UiSound::Click, "ui.click", "assets/sounds/ui/click.wav"},
    {UiSound::Toggle, "ui.toggle", "assets/sounds/ui/toggle.wav"},
    {UiSound::TabSwitch, "ui.tab_switch", "assets/sounds/ui/tab_switch.wav"},
    {UiSound::Confirm, "ui.confirm", "assets/sounds/ui/confirm.wav"},
    {UiSound::NodePlace, "ui.node_place", "assets/sounds/ui/node_place.wav"},
    {UiSound::Delete, "ui.delete", "assets/sounds/ui/delete.wav"},
    {UiSound::TrainPlace, "ui.train_place", "assets/sounds/ui/train_place.wav"},
    {UiSound::TrainRemove, "ui.delete", "assets/sounds/ui/delete.wav"},
    {UiSound::Warning, "ui.warning", "assets/sounds/ui/warning.wav"},
    {UiSound::Error, "ui.error", "assets/sounds/ui/error.wav"},
    {UiSound::Shutter, "ui.shutter", "assets/sounds/ui/shutter.wav"},
    {UiSound::Focus, "ui.focus", "assets/sounds/ui/focus.wav"},
};
static_assert(std::size(kUiSoundFiles) == kUiSoundCount);

// 同じidを持つ最初の表の行。2行目以降は、最初の行と同じ音として扱う(読込も再生も1回だけ)。
std::size_t FirstRowWithSameId(std::size_t row) {
    for (std::size_t i = 0; i < row; ++i) {
        if (std::string_view(kUiSoundFiles[i].id) == kUiSoundFiles[row].id) return i;
    }
    return row;
}

} // namespace

void UiSoundPlayer::Load(rs::sim::AudioEngine& engine) {
    for (std::size_t row = 0; row < std::size(kUiSoundFiles); ++row) {
        const UiSoundFile& file = kUiSoundFiles[row];
        const std::size_t first = FirstRowWithSameId(row);
        const std::size_t index = workspace::EnumIndex(file.sound);
        if (first != row) {
            loaded_[index] = loaded_[workspace::EnumIndex(kUiSoundFiles[first].sound)];
            continue;
        }
        const std::string path = rs::platform_file::ResolveAssetPath(file.path);
        if (path.empty()) continue;
        loaded_[index] = engine.LoadSound(file.id, rs::platform_text::Utf8ToWide(path));
    }
}

void UiSoundPlayer::Flush(rs::sim::AudioEngine& engine) {
    std::array<bool, kUiSoundCount> played{};
    for (std::size_t row = 0; row < std::size(kUiSoundFiles); ++row) {
        const UiSoundFile& file = kUiSoundFiles[row];
        const std::size_t index = workspace::EnumIndex(file.sound);
        if (!pending_[index]) continue;
        pending_[index] = false;
        const std::size_t first = FirstRowWithSameId(row);
        if (!loaded_[index] || played[first]) continue;
        played[first] = true;
        engine.PlaySound(file.id);
    }
}

} // namespace rs::app
