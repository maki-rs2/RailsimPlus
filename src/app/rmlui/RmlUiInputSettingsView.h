#pragma once

#include <optional>

#include <RmlUi/Core/DataModelHandle.h>

#include "../action/GameSettingsAction.h"
#include "../workspace/InputSettingsPresentation.h"

namespace rs::app { class ToastQueue; class Localization; }
namespace rs::app::rmlui {
class RmlUiConfirmDialog;

class RmlUiInputSettingsView {
public:
    explicit RmlUiInputSettingsView(ToastQueue& queue);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void RegisterPopups(RmlUiConfirmDialog& dialog) { dialog_ = &dialog; }
    // 設定と、割当表を差し替える先(refs.inputSource)を受け取る。呼び出し側が所有し、このビューより長く生きること。
    void Attach(const GameSettingsRefs& refs) { refs_.emplace(refs); }
    void SetActive(bool active);
    void CancelCapture();
    bool HandleKeyDown(unsigned int virtualKey, bool repeat);
    bool HandleMouseDown(int button);
    void RefreshTexts(const Localization& localization);

private:
    void Capture(const std::string& binding);
    // 割当表(設定の上書きを重ねたもの)から行を作り直して、画面へ反映する。
    void Rebuild();
    void Assign();
    void Reset(int row);
    void ResetAll();
    ToastQueue& queue_;
    std::optional<GameSettingsRefs> refs_;
    RmlUiConfirmDialog* dialog_ = nullptr;
    workspace::InputSettingsPresentation presentation_;
    Rml::DataModelHandle modelHandle_;
    bool active_ = false;
};
} // namespace rs::app::rmlui
