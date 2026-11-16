#pragma once

#include <optional>

#include <RmlUi/Core/DataModelHandle.h>
#include "../action/GameSettingsAction.h"
#include "../workspace/TimeControlPresentation.h"

namespace Rml { class ElementDocument; class Event; }
namespace rs::app { class ToastQueue; }

namespace rs::app::rmlui {
class RmlUiTimeControlView {
public:
    explicit RmlUiTimeControlView(ToastQueue& queue);
    // 操作の対象(ゲーム内時計)を渡す。呼び出し側が所有し、このビューより長く生きること。Attach前は時計と結ばない。
    void Attach(const GameSettingsRefs& refs) { refs_.emplace(refs); }
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void SetVisible(bool visible);
    // 毎フレーム呼ぶ。時計の値を表示へ写し(変わったときだけ画面へ伝える)、入力欄のフォーカス待ちを進める。
    void Refresh();
    bool CloseTransient();
    // 日時のウィンドウだけを閉じる(開いていなければ何もしない)。
    void CloseWindow();
private:
    void OnUi(Rml::Event& event, const Rml::VariantList& arguments);
    void OnEdit(Rml::Event& event);
    // 日時のウィンドウを開き、下書きへ今の日時・自転・公転を入れる。
    void OpenWindow();
    // 下書きの日時・自転・公転を反映して、ウィンドウを閉じる。
    void ConfirmWindow();
    ToastQueue& queue_;
    std::optional<GameSettingsRefs> refs_;
    workspace::TimeControlPresentation presentation_;
    Rml::DataModelHandle modelHandle_;
    Rml::ElementDocument* document_ = nullptr;
    bool pendingFocus_ = false;
};
}
