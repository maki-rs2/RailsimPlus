// RmlUiEdgeEditView.h - 敷設エディタの編集タブをworkspaceモデルへ結ぶ。
#pragma once

#include <memory>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>

namespace rs::editor { class Editor; class WarpLinkAuthoring; }
namespace rs::app { struct LayoutSession; class PluginSelector; }

namespace rs::app::rmlui {
struct EdgeEditModel;

class RmlUiEdgeEditView {
public:
    RmlUiEdgeEditView();
    ~RmlUiEdgeEditView();
    void Attach(rs::editor::Editor& editor, LayoutSession& session, PluginSelector& selector,
                rs::editor::WarpLinkAuthoring& warpAuthoring);
    void RegisterModel(Rml::DataModelConstructor& constructor);
    void Refresh();
private:
    void OnAction(Rml::Event& event, const Rml::VariantList& arguments);
    rs::editor::Editor* editor_ = nullptr;
    LayoutSession* session_ = nullptr;
    PluginSelector* selector_ = nullptr;
    rs::editor::WarpLinkAuthoring* warpAuthoring_ = nullptr;
    bool elevationFocused_ = false;
    Rml::DataModelHandle modelHandle_;
    std::unique_ptr<EdgeEditModel> model_;
};
}
