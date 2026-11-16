#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include "../workspace/ScenePanelPresentation.h"

namespace Rml { class ElementDocument; class Event; }
namespace rs::app { class LayoutPersistenceAction; class Localization; class ToastQueue; }

namespace rs::app::rmlui {
class RmlUiConfirmDialog;
class RmlUiScenePanelView {
public:
    explicit RmlUiScenePanelView(ToastQueue& queue);
    // 操作と一覧の元(レイアウトのシーン)を渡す。呼び出し側が所有し、このビューより長く生きること。
    // Attach前は一覧が空で、操作も受け付けない。
    void Attach(LayoutPersistenceAction& action, const Localization& localization) {
        action_ = &action;
        localization_ = &localization;
    }
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void RegisterPopups(RmlUiConfirmDialog& dialog) { dialog_ = &dialog; }
    void SetActive(bool active);
    void Refresh();
    bool CloseTransient();

private:
    void OnUi(Rml::Event& event, const Rml::VariantList& arguments);
    void OnAction(Rml::Event& event, const Rml::VariantList& arguments);
    void OnEdit(Rml::Event& event);
    void OpenDelete(int row);
    void ConfirmAdd();
    ToastQueue& queue_;
    LayoutPersistenceAction* action_ = nullptr;
    const Localization* localization_ = nullptr;
    workspace::ScenePanelPresentation presentation_;
    Rml::DataModelHandle modelHandle_;
    RmlUiConfirmDialog* dialog_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    bool pendingFocus_ = false;
};
}
