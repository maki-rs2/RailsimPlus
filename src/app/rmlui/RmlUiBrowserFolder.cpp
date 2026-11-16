#include "RmlUiBrowserFolder.h"

#include <algorithm>
#include <format>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include "../action/PluginFolderMoveAction.h"

#include "RmlUiDataEventArgs.h"
#include "RmlUiDragDrop.h"
#include "RmlUiPluginBrowserModel.h"
#include "RmlUiUpdateVariable.h"

namespace rs::app::rmlui {
namespace {

constexpr const char* kFoldersKey = "browserFolders";
constexpr const char* kCrumbsKey = "browserCrumbs";
constexpr const char* kBackDepthKey = "browserBackDepth";
// ドラッグ元のカードの名前・フォルダのカードの添字を持つ属性(workspace.rmlのカード)。
constexpr const char* kDragNameAttribute = "data-drag-name";
constexpr const char* kFolderIndexAttribute = "data-folder-index";

} // namespace

void RmlUiBrowserFolder::Register(Rml::DataModelConstructor& constructor, PluginBrowserModel& model,
                                  std::function<void()> onNavigated) {
    constructor.Bind(kFoldersKey, &model.folders);
    constructor.Bind(kCrumbsKey, &model.crumbs);
    constructor.Bind(kBackDepthKey, &model.backDepth);
    // 引数はFolders()の添字 / 最上位からの深さ(カードのidとは別のイベントにして混ぜない)。
    constructor.BindEventCallback("enter_browser_folder",
                                  [this, onNavigated](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      const int index = FirstEventArgument<int>(arguments, -1);
                                      if (state_ == nullptr || index < 0) return;
                                      state_->EnterFolder(static_cast<std::size_t>(index));
                                      onNavigated();
                                  });
    constructor.BindEventCallback("navigate_browser_path",
                                  [this, onNavigated](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& arguments) {
                                      const int depth = FirstEventArgument<int>(arguments, -1);
                                      if (state_ == nullptr || depth < 0) return;
                                      state_->NavigateToDepth(static_cast<std::size_t>(depth));
                                      onNavigated();
                                  });
    // ドラッグ中のカードが乗っている間だけ、落とせる場所を強調する。
    constructor.BindEventCallback("browser_drag_over", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
        SetDropHighlight(event, true);
    });
    constructor.BindEventCallback("browser_drag_out", [](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList&) {
        SetDropHighlight(event, false);
    });
    // 引数はFolders()の添字(フォルダのカード)/ 最上位からの深さ(パンくず・戻るボタン)。
    constructor.BindEventCallback("browser_drop_folder", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
        SetDropHighlight(event, false);
        const int index = FirstEventArgument<int>(arguments, -1);
        if (state_ == nullptr || index < 0 || static_cast<std::size_t>(index) >= state_->Folders().size()) return;
        DropOn(event, state_->Folders()[static_cast<std::size_t>(index)].name, index);
    });
    constructor.BindEventCallback("browser_drop_path", [this](Rml::DataModelHandle, Rml::Event& event, const Rml::VariantList& arguments) {
        SetDropHighlight(event, false);
        const int depth = FirstEventArgument<int>(arguments, -1);
        if (state_ == nullptr || depth < 0) return;
        const std::vector<std::string> labels = state_->PathLabels();
        if (static_cast<std::size_t>(depth) >= labels.size()) return;
        DropOn(event, labels[static_cast<std::size_t>(depth)], -1);
    });
}

void RmlUiBrowserFolder::DropOn(Rml::Event& event, const std::string& target, int folderIndex) {
    Rml::Element* const dragged = DraggedElement(event);
    if (queue_ == nullptr || dragged == nullptr) return;
    const Rml::String source = dragged->GetAttribute<Rml::String>(kDragNameAttribute, "");
    if (source.empty()) return;
    if (folderIndex >= 0 && dragged->GetAttribute<int>(kFolderIndexAttribute, -1) == folderIndex) return;
    plugin_folder_move_action::MoveToFolder(*queue_, source, target);
}

void RmlUiBrowserFolder::Refresh(Rml::DataModelHandle& handle, PluginBrowserModel& model,
                                 const plugin_browser::CardRange& build) {
    Rml::Vector<BrowserFolderModel> folders;
    Rml::Vector<BrowserCrumbModel> crumbs;
    int backDepth = -1;
    if (state_ != nullptr && state_->ShowsHierarchy()) {
        const std::vector<plugin_browser::PluginTreeFolder>& all = state_->Folders();
        for (std::size_t i = build.first; i < std::min(build.last, all.size()); ++i) {
            folders.push_back({static_cast<int>(i), all[i].name, std::format("{}件", all[i].count)});
        }
        const std::vector<std::string> labels = state_->PathLabels();
        for (std::size_t depth = 0; depth < labels.size(); ++depth) {
            crumbs.push_back({static_cast<int>(depth), labels[depth], depth + 1 == labels.size()});
        }
        // 戻り先は1つ上の階層(最上位にいるときは無い)。
        if (labels.size() >= 2) {
            backDepth = static_cast<int>(labels.size()) - 2;
        }
    }
    UpdateVariable(handle, kFoldersKey, model.folders, folders);
    UpdateVariable(handle, kCrumbsKey, model.crumbs, crumbs);
    UpdateVariable(handle, kBackDepthKey, model.backDepth, backDepth);
}

} // namespace rs::app::rmlui
