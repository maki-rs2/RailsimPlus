#include "RmlUiInputSettingsView.h"

#include <windows.h>

#include <iterator>
#include <optional>
#include <utility>

#include <RmlUi/Core/DataStructHandle.h>
#include <RmlUi/Core/Input.h>

#include "RmlUiConfirmDialog.h"
#include "RmlUiDataEventArgs.h"
#include "RmlUiKeyboardRouter.h"
#include "../action/GameSettingsScreenModel.h"
#include "../action/SettingsPlaceholderAction.h"
#include "../action/InputBindingAction.h"
#include "../action/ToastQueue.h"
#include "../input/InputBindings.h"

namespace rs::app::rmlui {
using namespace workspace;

namespace {

struct VirtualKeyEntry {
    unsigned int virtualKey;
    input::InputKey key;
};

// 割り当てられるキー(Escは入力待ちの取り消しに使うので含めない)。
constexpr VirtualKeyEntry kVirtualKeys[] = {
    {'W', input::InputKey::W},           {'A', input::InputKey::A},
    {'S', input::InputKey::S},           {'D', input::InputKey::D},
    {'Z', input::InputKey::Z},           {'Y', input::InputKey::Y},
    {'R', input::InputKey::R},           {VK_DELETE, input::InputKey::Delete},
    {VK_UP, input::InputKey::UpArrow},   {VK_DOWN, input::InputKey::DownArrow},
    {VK_LEFT, input::InputKey::LeftArrow}, {VK_RIGHT, input::InputKey::RightArrow},
    {VK_SPACE, input::InputKey::Space},  {VK_F1, input::InputKey::F1},
    {VK_F2, input::InputKey::F2},        {VK_F3, input::InputKey::F3},
    {VK_F4, input::InputKey::F4},        {VK_F5, input::InputKey::F5},
    {VK_F12, input::InputKey::F12},   {VK_F11, input::InputKey::F11},
    {'Q', input::InputKey::Q},          {'E', input::InputKey::E},
};
static_assert(std::size(kVirtualKeys) + 1 == static_cast<std::size_t>(input::InputKey::Count),
              "Esc以外のInputKeyがすべて仮想キーと対応していること");

std::optional<input::InputKey> InputKeyFromVirtualKey(unsigned int virtualKey) {
    for (const VirtualKeyEntry& entry : kVirtualKeys) {
        if (entry.virtualKey == virtualKey) return entry.key;
    }
    return std::nullopt;
}

input::KeyModifiers CurrentModifiers() {
    const int state = KeyModifierState();
    return {(state & Rml::Input::KM_CTRL) != 0, (state & Rml::Input::KM_SHIFT) != 0,
            (state & Rml::Input::KM_ALT) != 0};
}

} // namespace

RmlUiInputSettingsView::RmlUiInputSettingsView(ToastQueue& queue)
    : queue_(queue) {
    // マウス操作の切り替えは実設定につながっていない仮の値。
    presentation_.mouseMode_ = "wasd";
}

void RmlUiInputSettingsView::RegisterModel(Rml::DataModelConstructor& constructor) {
    Rml::StructHandle<InputSettingsRow> row = constructor.RegisterStruct<InputSettingsRow>();
    row.RegisterMember("section", &InputSettingsRow::section_);
    row.RegisterMember("operation", &InputSettingsRow::operation_);
    row.RegisterMember("binding", &InputSettingsRow::binding_);
    row.RegisterMember("warning", &InputSettingsRow::warning_);
    row.RegisterMember("waiting", &InputSettingsRow::waiting_);
    constructor.RegisterArray<std::vector<InputSettingsRow>>();
    Rml::StructHandle<InputSettingsPresentation> handle = constructor.RegisterStruct<InputSettingsPresentation>();
    handle.RegisterMember("mouseMode", &InputSettingsPresentation::mouseMode_);
    handle.RegisterMember("mouseLabel", &InputSettingsPresentation::mouseLabel_);
    handle.RegisterMember("keysLabel", &InputSettingsPresentation::keysLabel_);
    handle.RegisterMember("waitingLabel", &InputSettingsPresentation::waitingLabel_);
    handle.RegisterMember("resetLabel", &InputSettingsPresentation::resetLabel_);
    handle.RegisterMember("resetAllLabel", &InputSettingsPresentation::resetAllLabel_);
    handle.RegisterMember("warningLabel", &InputSettingsPresentation::warningLabel_);
    handle.RegisterMember("unassignedLabel", &InputSettingsPresentation::unassignedLabel_);
    handle.RegisterMember("note", &InputSettingsPresentation::note_);
    handle.RegisterMember("mouseChoices", &InputSettingsPresentation::mouseChoices_);
    handle.RegisterMember("rows", &InputSettingsPresentation::rows_);
    constructor.Bind("gsInput", &presentation_);
    constructor.BindEventCallback("gs_input_begin", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& arguments) {
        if (active_ && presentation_.BeginCapture(FirstEventArgument<int>(arguments, -1)))
            modelHandle_.DirtyVariable("gsInput");
    });
    constructor.BindEventCallback("gs_mouse_mode", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& arguments) {
        game_settings_action::SetMouseOperation(queue_, FirstEventArgument<std::string>(arguments, ""));
    });
    constructor.BindEventCallback("gs_input_reset", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList& arguments) {
        Reset(FirstEventArgument<int>(arguments, -1));
    });
    constructor.BindEventCallback("gs_input_reset_all", [this](Rml::DataModelHandle, Rml::Event&,
        const Rml::VariantList&) { ResetAll(); });
    modelHandle_ = constructor.GetModelHandle();
}

void RmlUiInputSettingsView::RefreshTexts(const Localization& localization) {
    if (!refs_.has_value()) return;
    presentation_ = BuildInputSettingsPresentation(
        presentation_, localization, InputBindingAction(refs_->userSettings, refs_->inputSource).Bindings());
    if (modelHandle_) modelHandle_.DirtyVariable("gsInput");
}

void RmlUiInputSettingsView::Rebuild() {
    if (refs_.has_value()) RefreshTexts(refs_->localization);
}

void RmlUiInputSettingsView::Assign() {
    if (!refs_.has_value()) return;
    // 確認を経て重なる行があれば、その行を未割当にしてから割り当てる(重ならなければ空)。
    const bool assigned =
        InputBindingAction(refs_->userSettings, refs_->inputSource)
            .AssignUnassigning(presentation_.waitingRow_, presentation_.pendingBinding_,
                               presentation_.ConflictingRows(presentation_.waitingRow_, presentation_.pendingBinding_));
    if (!assigned) queue_.Post(ToastSeverity::Warning, presentation_.unassignableMessage_);
    CancelCapture();
    Rebuild();
}

void RmlUiInputSettingsView::Reset(int row) {
    if (!refs_.has_value()) return;
    CancelCapture();
    InputBindingAction(refs_->userSettings, refs_->inputSource).Reset(row);
    Rebuild();
}

void RmlUiInputSettingsView::ResetAll() {
    if (!refs_.has_value() || dialog_ == nullptr) return;
    CancelCapture();
    ConfirmDialogContent content;
    content.message = presentation_.resetAllConfirmMessage_;
    content.confirmLabel = presentation_.resetAllLabel_;
    content.cancelLabel = presentation_.cancelLabel_;
    content.onConfirm = [this] {
        InputBindingAction(refs_->userSettings, refs_->inputSource).ResetAll();
        Rebuild();
    };
    dialog_->Open(std::move(content));
}

void RmlUiInputSettingsView::SetActive(bool active) {
    active_ = active;
    if (!active) CancelCapture();
}

void RmlUiInputSettingsView::CancelCapture() {
    if (presentation_.waitingRow_ < 0) return;
    presentation_.CancelCapture();
    if (modelHandle_) modelHandle_.DirtyVariable("gsInput");
}

bool RmlUiInputSettingsView::HandleKeyDown(unsigned int virtualKey, bool repeat) {
    if (!active_ || presentation_.waitingRow_ < 0) return false;
    if (repeat) return true;
    if (virtualKey == VK_ESCAPE) {
        Capture("Esc");
        return true;
    }
    if (virtualKey == VK_SHIFT || virtualKey == VK_CONTROL || virtualKey == VK_MENU) return true;
    const std::optional<input::InputKey> key = InputKeyFromVirtualKey(virtualKey);
    if (!key.has_value()) {
        // 割り当てられないキーは捕捉せず、入力待ちのまま知らせる。
        queue_.Post(ToastSeverity::Warning, presentation_.unassignableMessage_);
        return true;
    }
    Capture(input::FormatKeyChord({*key, CurrentModifiers()}));
    return true;
}

bool RmlUiInputSettingsView::HandleMouseDown(int button) {
    if (!active_ || presentation_.waitingRow_ < 0) return false;
    // マウスのボタンは割り当てに使えない(クリックは入力待ちの間だけここで受ける)。
    queue_.Post(ToastSeverity::Warning, presentation_.unassignableMessage_);
    return true;
}

void RmlUiInputSettingsView::Capture(const std::string& binding) {
    const InputSettingsCapture result = presentation_.Capture(binding);
    if (result == InputSettingsCapture::Conflict && dialog_ != nullptr) {
        ConfirmDialogContent content;
        content.message = presentation_.confirmMessage_;
        content.confirmLabel = presentation_.confirmLabel_;
        content.cancelLabel = presentation_.cancelLabel_;
        content.onConfirm = [this] { Assign(); };
        content.onCancel = [this] { CancelCapture(); };
        dialog_->Open(std::move(content));
    } else if (result == InputSettingsCapture::Ready) {
        Assign();
    } else if (result == InputSettingsCapture::Reserved) {
        // 固定の割り当てとは重なれない。入力待ちのまま知らせる。
        queue_.Post(ToastSeverity::Warning, presentation_.reservedKeyMessage_);
    }
    modelHandle_.DirtyVariable("gsInput");
}
} // namespace rs::app::rmlui
