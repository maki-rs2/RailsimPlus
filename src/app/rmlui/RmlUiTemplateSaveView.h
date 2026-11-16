#pragma once

#include <string>
#include <vector>

#include <RmlUi/Core/DataModelHandle.h>

#include "../workspace/ContextMenuCatalog.h"
#include "../workspace/TemplateSavePresentation.h"

namespace Rml { class Context; class Event; }
namespace rs::editor { class Editor; }
namespace rs::app {
class PluginSelector;
class ToastQueue;
}

namespace rs::app::rmlui {
class RmlUiContextMenu;
class RmlUiConfirmDialog;

class RmlUiTemplateSaveView : public workspace::IContextMenuProvider {
public:
    explicit RmlUiTemplateSaveView(ToastQueue& queue);
    // 保存が使う参照。呼び出し側が所有し、このビューより長く生きること。pluginRootsは登録順で、先頭の下へ保存する。
    void Attach(rs::editor::Editor& editor, PluginSelector& selector, const std::vector<std::string>& pluginRoots);
    void RegisterModel(Rml::DataModelConstructor& constructor, Rml::Context& context);
    void RegisterPopups(RmlUiContextMenu& menu, RmlUiConfirmDialog& dialog);
    void Begin(workspace::TemplateSaveTarget target, const std::string& targetId);
    void Refresh();
    void SetActive(bool rail, bool train);
    bool CloseTransient();
    bool IsEditingFormation() const { return !presentation_.formationId_.empty(); }
    void OnEdit(Rml::Event& event);

    bool TargetExists(workspace::ContextMenuKind kind, const std::string& targetId) const override;
    bool IsItemEnabled(workspace::ContextMenuKind kind, std::string_view itemId,
                       const std::string& targetId) const override;
    void OnItemChosen(workspace::ContextMenuKind kind, std::string_view itemId,
                      const std::string& targetId) override;

private:
    void Run(const workspace::TemplateSaveRequest& request);
    void OpenOverwrite();
    ToastQueue& queue_;
    rs::editor::Editor* editor_ = nullptr;
    PluginSelector* selector_ = nullptr;
    const std::vector<std::string>* pluginRoots_ = nullptr;
    workspace::TemplateSavePresentation presentation_;
    Rml::DataModelHandle modelHandle_;
    Rml::Context* context_ = nullptr;
    RmlUiConfirmDialog* dialog_ = nullptr;
    bool pendingFocus_ = false;
    bool railActive_ = false;
};
} // namespace rs::app::rmlui
