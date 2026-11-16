#include "PluginCatalog.h"

#include <algorithm>
#include <iterator>
#include <cctype>

#include <spdlog/spdlog.h>

#include "../plugin_session/PluginSelector.h"
#include "../../platform/text/AsciiCase.h"

namespace rs::app::plugin_browser {

namespace {

using rs::platform_text::ToAsciiLower;

// パスの最後のフォルダ名(末尾の区切りは無視する)。
std::string LeafName(const std::string& path) {
    std::size_t end = path.size();
    while (end > 0 && (path[end - 1] == '/' || path[end - 1] == '\\')) --end;
    const std::size_t separator = path.find_last_of("/\\", end == 0 ? 0 : end - 1);
    const std::size_t begin = (separator == std::string::npos || separator >= end) ? 0 : separator + 1;
    return path.substr(begin, end - begin);
}

// 同じ相対パスのうち、勝つのはルートの添字が小さいほう。同じルートなら絶対パスの小さいほう
// (どの順で届いても同じ側が勝つようにする)。
bool WinsOver(std::size_t rootIndexA, const std::string& folderA, std::size_t rootIndexB, const std::string& folderB) {
    if (rootIndexA != rootIndexB) return rootIndexA < rootIndexB;
    return folderA < folderB;
}

// 重複なく入れ、整列した状態を保つ。入れたらtrue。
bool InsertShadowed(std::vector<std::string>& shadowed, const std::string& folder) {
    std::vector<std::string>::iterator it = std::lower_bound(shadowed.begin(), shadowed.end(), folder);
    if (it != shadowed.end() && *it == folder) return false;
    shadowed.insert(it, folder);
    return true;
}

PluginCatalogEntry MakeEntry(const PluginScanResult& result) {
    PluginCatalogEntry entry;
    entry.kind = result.kind;
    entry.hasSignal = result.header.hasSignal;
    entry.relativeKey = result.relativeKey;
    entry.folderUtf8 = result.folderUtf8;
    entry.rootIndex = result.rootIndex;
    entry.name = result.header.header.name;
    entry.author = result.header.header.author;
    entry.description = result.header.header.description;
    entry.iconPathUtf8 = result.header.iconPathUtf8;
    std::copy(std::begin(result.header.header.iconRect), std::end(result.header.header.iconRect),
              std::begin(entry.iconRect));
    return entry;
}

} // namespace

void PluginCatalog::Merge(const PluginScanResult& result) {
    if (result.generation != generation_) return;
    if (!result.header.ok || result.header.noDefinition) {
        spdlog::debug("[catalog] skip (no readable header): {}", result.folderUtf8);
        return;
    }
    const std::optional<BrowserKind> headerKind = BrowserKindFromPluginType(result.header.header.type);
    if (!headerKind.has_value() || *headerKind != result.kind) {
        spdlog::debug("[catalog] skip (type mismatch, type={}): {}", result.header.header.typeRaw, result.folderUtf8);
        return;
    }

    const std::string lowerKey = ToAsciiLower(result.relativeKey);
    const std::unordered_map<std::string, std::size_t>::const_iterator found = indexByKey_.find(lowerKey);
    if (found == indexByKey_.end()) {
        entries_.push_back(MakeEntry(result));
        const std::size_t entryId = entries_.size() - 1;
        indexByKey_.emplace(lowerKey, entryId);
        indexByLeaf_.emplace(ToAsciiLower(LeafName(result.relativeKey)), entryId);
        ++revision_;
        return;
    }

    PluginCatalogEntry& existing = entries_[found->second];
    if (existing.folderUtf8 == result.folderUtf8) return; // 同じ結果の再到着
    if (WinsOver(result.rootIndex, result.folderUtf8, existing.rootIndex, existing.folderUtf8)) {
        // 新しいほうが勝つ。負けた側を影へ回し、表示用の項目は新しいほうで入れ替える。
        std::vector<std::string> shadowed = std::move(existing.shadowedFoldersUtf8);
        InsertShadowed(shadowed, existing.folderUtf8);
        shadowed.erase(std::remove(shadowed.begin(), shadowed.end(), result.folderUtf8), shadowed.end());
        existing = MakeEntry(result);
        existing.shadowedFoldersUtf8 = std::move(shadowed);
        ++revision_;
    } else if (InsertShadowed(existing.shadowedFoldersUtf8, result.folderUtf8)) {
        ++revision_;
    }
}

void PluginCatalog::Reset() {
    entries_.clear();
    indexByKey_.clear();
    indexByLeaf_.clear();
    ++generation_;
    ++revision_;
}

const PluginCatalogEntry* PluginCatalog::Find(const std::string& folderUtf8) const {
    const std::pair<std::unordered_multimap<std::string, std::size_t>::const_iterator,
                    std::unordered_multimap<std::string, std::size_t>::const_iterator>
        range = indexByLeaf_.equal_range(ToAsciiLower(LeafName(folderUtf8)));
    for (std::unordered_multimap<std::string, std::size_t>::const_iterator it = range.first; it != range.second; ++it) {
        const PluginCatalogEntry& entry = entries_[it->second];
        if (rs::app::SamePluginPath(entry.folderUtf8, folderUtf8)) return &entry;
    }
    return nullptr;
}

std::optional<std::size_t> PluginCatalog::FindByRelativeKey(const std::string& relativeKey) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator found =
        indexByKey_.find(ToAsciiLower(relativeKey));
    if (found == indexByKey_.end()) return std::nullopt;
    return found->second;
}

} // namespace rs::app::plugin_browser
