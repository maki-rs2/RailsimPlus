#include "MissingPluginCatalog.h"

#include <filesystem>
#include <map>
#include <unordered_map>
#include <utility>

#include "../../editor/tool/Editor.h"
#include "../../platform/text/Encoding.h"
#include "../../world/scene/SceneRegistry.h"
#include "LayoutSession.h"
#include "PluginSelector.h"

namespace rs::app {

bool PluginFolderExists(const std::string& folderUtf8) {
    std::error_code error;
    return std::filesystem::is_directory(std::filesystem::path(rs::platform_text::Utf8ToWide(folderUtf8)), error);
}

std::vector<PluginReference> CollectPluginReferences(
    const rs::editor::Editor& editor, const rs::world::SceneRegistry& sceneRegistry,
    const LayoutSession& session, const PluginSelector& selector,
    const std::vector<ImportedWireSpan>* importedWireSpans, const std::vector<std::string>* pluginRoots) {
    std::vector<PluginReference> references;
    const ConstPluginReferenceContext context{editor, sceneRegistry, session, selector, importedWireSpans,
                                              pluginRoots};
    VisitPluginReferences(context, [&references](PluginReferenceSlot& slot) {
        references.push_back({slot.folder, slot.site});
        return false;
    });
    return references;
}

std::vector<MissingPluginEntry> AggregateMissingPlugins(const std::vector<PluginReference>& references,
                                                        const PluginExistsFn& exists) {
    // mapのキー順(正規化したフォルダ名の昇順→site)がそのまま結果の順になる。
    struct Row {
        std::string folderUtf8;  // 最初に見つかった表記
        size_t count = 0;
    };
    std::map<std::pair<std::string, PluginReferenceSite>, Row> rows;
    std::unordered_map<std::string, bool> existsByKey;  // 同じフォルダを何度も調べない
    for (const PluginReference& reference : references) {
        if (reference.folderUtf8.empty()) continue;
        const std::string key = PluginFolderKey(reference.folderUtf8);
        std::unordered_map<std::string, bool>::iterator cached = existsByKey.find(key);
        if (cached == existsByKey.end()) {
            cached = existsByKey.emplace(key, exists(reference.folderUtf8)).first;
        }
        if (cached->second) continue;
        Row& row = rows[{key, reference.site}];
        if (row.count == 0) row.folderUtf8 = reference.folderUtf8;
        ++row.count;
    }

    std::vector<MissingPluginEntry> result;
    result.reserve(rows.size());
    for (const std::pair<const std::pair<std::string, PluginReferenceSite>, Row>& entry : rows) {
        result.push_back({entry.second.folderUtf8, entry.first.second, entry.second.count});
    }
    return result;
}

}  // namespace rs::app
