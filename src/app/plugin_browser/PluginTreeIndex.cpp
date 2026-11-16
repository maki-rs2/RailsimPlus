#include "PluginTreeIndex.h"

#include <algorithm>
#include <optional>

#include "../../platform/text/AsciiCase.h"
#include "PluginBrowserState.h"

namespace rs::app::plugin_browser {

void PluginTreeIndex::FillNode(Node& node, const rs::legacy_layout::PluginTreeDir& dir, BrowserKind kind,
                               const PluginCatalog& catalog, std::unordered_set<EntryId>& listed) {
    const char* const folderName = GetBrowserKind(kind).folderName;
    for (const rs::legacy_layout::PluginTreeFile& file : dir.files) {
        // 木にはブラウザの対象でない種別(編成のひな形など)も混ざるので、このタブの種別だけ使う。
        if (!rs::platform_text::EqualsIgnoreAsciiCase(file.kind, folderName)) continue;
        const std::optional<std::size_t> id = catalog.FindByRelativeKey(std::string(folderName) + "/" + file.name);
        if (!id.has_value() || !EntryMatchesKind(catalog.Entries()[*id], kind)) continue;
        if (std::find(node.entries.begin(), node.entries.end(), *id) != node.entries.end()) continue;
        node.entries.push_back(*id);
        listed.insert(*id);
    }
    node.count = node.entries.size();

    for (const rs::legacy_layout::PluginTreeDir& childDir : dir.dirs) {
        const bool duplicated = std::any_of(node.children.begin(), node.children.end(),
                                            [&childDir](const Node& c) { return c.name == childDir.name; });
        if (duplicated) continue; // 同名の兄弟は先に出たものを使う
        Node child;
        child.name = childDir.name;
        FillNode(child, childDir, kind, catalog, listed);
        if (child.count == 0) continue;
        node.count += child.count;
        node.children.push_back(std::move(child));
    }
}

void PluginTreeIndex::Build(const rs::legacy_layout::PluginTreeDir* tree, BrowserKind kind,
                            const PluginCatalog& catalog) {
    root_ = Node{};
    hasTree_ = tree != nullptr;
    if (!hasTree_) return;

    std::unordered_set<EntryId> listed;
    FillNode(root_, *tree, kind, catalog, listed);

    // 木に載っていないエントリは最上位の直下に出す。
    const std::vector<PluginCatalogEntry>& entries = catalog.Entries();
    for (EntryId id = 0; id < entries.size(); ++id) {
        if (listed.count(id) != 0 || !EntryMatchesKind(entries[id], kind)) continue;
        root_.entries.push_back(id);
        ++root_.count;
    }
}

const PluginTreeIndex::Node* PluginTreeIndex::Find(const std::vector<std::string>& path) const {
    if (!hasTree_) return nullptr;
    const Node* node = &root_;
    for (const std::string& name : path) {
        const std::vector<Node>::const_iterator it =
            std::find_if(node->children.begin(), node->children.end(), [&name](const Node& c) { return c.name == name; });
        if (it == node->children.end()) return nullptr;
        node = &*it;
    }
    return node;
}

bool PluginTreeIndex::Contains(const std::vector<std::string>& path) const {
    return Find(path) != nullptr;
}

std::vector<PluginTreeFolder> PluginTreeIndex::FoldersAt(const std::vector<std::string>& path) const {
    std::vector<PluginTreeFolder> folders;
    const Node* node = Find(path);
    if (node == nullptr) return folders;
    for (const Node& child : node->children) folders.push_back({child.name, child.count});
    return folders;
}

std::vector<EntryId> PluginTreeIndex::EntriesAt(const std::vector<std::string>& path) const {
    const Node* node = Find(path);
    return node != nullptr ? node->entries : std::vector<EntryId>{};
}

} // namespace rs::app::plugin_browser
