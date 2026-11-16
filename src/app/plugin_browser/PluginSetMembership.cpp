#include "PluginSetMembership.h"

#include <algorithm>

#include "../plugin_session/PluginSelector.h"

namespace rs::app::plugin_browser {

void PluginSetMembership::SetFolders(const std::vector<std::string>& setFolders) {
    if (setFolders == setFolders_) return;
    setFolders_ = setFolders;
    cache_.clear();
    ++revision_;
}

bool PluginSetMembership::Contains(EntryId id, const PluginCatalogEntry& entry) {
    if (setFolders_.empty()) return false; // 空なら比べるまでもない(ファイルシステムに触らない)
    const std::unordered_map<EntryId, Remembered>::const_iterator found = cache_.find(id);
    if (found != cache_.end() && found->second.folderUtf8 == entry.folderUtf8) return found->second.inSet;

    const bool inSet = std::any_of(setFolders_.begin(), setFolders_.end(), [&entry](const std::string& folder) {
        return rs::app::SamePluginPath(folder, entry.folderUtf8);
    });
    cache_[id] = Remembered{entry.folderUtf8, inSet};
    return inSet;
}

} // namespace rs::app::plugin_browser
