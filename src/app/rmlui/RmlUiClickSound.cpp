#include "RmlUiClickSound.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>

#include "../audio/UiSoundPlayer.h"

namespace rs::app::rmlui {

namespace {
// 確認ダイアログの確定ボタン(RmlUiConfirmDialog.cppのkConfirmId)。
constexpr const char* kConfirmButtonId = "confirm-dialog-ok";

// タブを切り替えるボタンのクラス。
constexpr std::array<std::string_view, 2> kTabButtonClasses = {"browser-tab", "game-settings-tab"};
// 子のsegmentがタブになる入れ物のクラス(segmentedで作ったタブ)。
constexpr std::array<std::string_view, 2> kTabSegmentedClasses = {"layout-files-tabs", "rail-tabs"};
constexpr std::string_view kSegmentClass = "segment";
// トグルとして鳴らすinputのtype(rangeなど連続操作は鳴らさない)。
constexpr std::array<std::string_view, 2> kToggleInputTypes = {"checkbox", "radio"};

bool HasAnyClass(const Rml::Element& element, std::span<const std::string_view> classes) {
    return std::ranges::any_of(classes, [&element](std::string_view name) {
        return element.IsClassSet(Rml::String(name));
    });
}

// 要素1つに対する音。音の対象でない要素は音なし・stopなしで返し、親へたどる。
struct ElementSound {
    std::optional<UiSound> sound;
    bool stop = false; // trueなら親へたどらず終わる(対象の要素と、無効な要素)
};

ElementSound SoundOf(const Rml::Element& element) {
    const Rml::String& tag = element.GetTagName();
    if (tag == "button") {
        if (element.HasAttribute("disabled")) return {std::nullopt, true};
        if (element.GetId() == kConfirmButtonId) return {UiSound::Confirm, true};
        if (HasAnyClass(element, kTabButtonClasses)) return {UiSound::TabSwitch, true};
        return {UiSound::Click, true};
    }
    if (tag == "input") {
        const Rml::String type = element.GetAttribute<Rml::String>("type", "");
        if (std::ranges::find(kToggleInputTypes, std::string_view(type)) == kToggleInputTypes.end()) return {};
        if (element.HasAttribute("disabled")) return {std::nullopt, true};
        return {UiSound::Toggle, true};
    }
    if (element.IsClassSet(Rml::String(kSegmentClass)) && element.GetParentNode() != nullptr &&
        HasAnyClass(*element.GetParentNode(), kTabSegmentedClasses)) {
        return {UiSound::TabSwitch, true};
    }
    return {};
}
} // namespace

RmlUiClickSound::RmlUiClickSound(Rml::ElementDocument& document, std::function<void(UiSound)> onSound)
    : document_(document), onSound_(std::move(onSound)) {
    document_.AddEventListener("click", this, false);
}

RmlUiClickSound::~RmlUiClickSound() {
    document_.RemoveEventListener("click", this, false);
}

// labelを押したときは、RmlUiがlabelの対象(input)へ押下を転送し、元の押下は止める。
// そのため、ここで受けるのは転送後のinputの押下だけで、トグルは二重に鳴らない。
void RmlUiClickSound::ProcessEvent(Rml::Event& event) {
    for (Rml::Element* element = event.GetTargetElement(); element != nullptr; element = element->GetParentNode()) {
        const ElementSound result = SoundOf(*element);
        if (result.sound.has_value() && onSound_) onSound_(*result.sound);
        if (result.stop) return;
    }
}

} // namespace rs::app::rmlui
