//
// RmlUiBrowserSelected.cpp
//
#include "RmlUiBrowserSelected.h"

#include "../../editor/tool/Editor.h"
#include "../plugin_browser/PluginCatalog.h"
#include "RmlUiPluginBrowserModel.h"

namespace rs::app::rmlui {

void RmlUiBrowserSelected::Update(const plugin_browser::PluginCatalog& catalog,
                                  const rs::editor::Editor& editor,
                                  plugin_browser::BrowserSelectedFolder source) {
    static const std::string kNone;
    const std::string* activeFolder = &kNone;
    switch (source) {
    case plugin_browser::BrowserSelectedFolder::None:
        break;
    case plugin_browser::BrowserSelectedFolder::SceneryPlacement:
        if (editor.sceneryPlacementMode) activeFolder = &editor.sceneryPlacementFolder;
        break;
    case plugin_browser::BrowserSelectedFolder::SignalPlugin:
        activeFolder = &editor.signalPluginFolder;
        break;
    }
    const std::string& active = *activeFolder;
    if (active.empty()) {
        folder_.clear();
        name_.clear();
        resolved_ = false;
        return;
    }
    if (folder_ == active && resolved_ && resolvedRevision_ == catalog.Revision()) return;
    folder_ = active;
    resolvedRevision_ = catalog.Revision();
    resolved_ = true;
    const plugin_browser::PluginCatalogEntry* entry = catalog.Find(active);
    if (entry != nullptr) {
        name_ = BrowserEntryDisplayName(*entry);
        return;
    }
    // カタログに無い(走査前など)ときは、フォルダ名の末尾で代える。
    const std::size_t slash = active.find_last_of("/\\");
    name_ = slash == std::string::npos ? active : active.substr(slash + 1);
}

} // namespace rs::app::rmlui
