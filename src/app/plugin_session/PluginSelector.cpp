//
// PluginSelector.cpp
//
#include "PluginSelector.h"
#include "PluginReferenceWalk.h"
#include "../../world/alignment/TrackGraph.h"

#include "../../plugins/schema/PluginTypeTraits.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "../../plugins/load/PluginPaths.h"
#include "../scene/ParsedPluginCache.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {

namespace {

// プラグインを読めなかったときの説明。フォルダ自体が無い(未インストール)のか、
// フォルダはあるのに定義が読めないのかを分けて伝える。
std::string LoadFailureMessage(const std::string& folderUtf8) {
    std::error_code ec;
    const bool folderExists =
        std::filesystem::is_directory(rs::platform_text::Utf8ToWide(folderUtf8), ec);
    return (folderExists ? std::string("プラグイン定義の読込に失敗しました: ")
                         : std::string("プラグインのフォルダがありません(未インストール): ")) +
           folderUtf8;
}

bool IsTrackPlugin(rs::legacy_pi::PluginType type) {
    return rs::legacy_pi::GetPluginTypeTraits(type).trackPlugin;
}

// 定義のヘッダの名前。空ならフォルダ名。
std::string DisplayNameOf(const rs::legacy_pi::PluginDefinition& def, const std::string& folderUtf8) {
    if (!def.header.name.empty()) return def.header.name;
    const std::filesystem::path folderPath(rs::platform_text::Utf8ToWide(folderUtf8));
    return rs::platform_text::WideToUtf8(folderPath.filename().wstring());
}

// 編成の1両をTrainプラグインの定義から作る(連結位置とスイッチ数をキャッシュする)。
FormationCar MakeFormationCar(const std::string& folderUtf8, const std::string& displayName,
                              const rs::legacy_pi::PluginDefinition& def) {
    FormationCar entry;
    entry.folderUtf8 = folderUtf8;
    entry.reversed = false;
    entry.displayName = displayName;
    entry.frontLimit = def.trainInfo.frontLimit;
    entry.doorClosingTime = def.trainInfo.doorClosingTime;
    entry.tailLimit = def.trainInfo.tailLimit;
    entry.pluginSwitchCount = def.switches.size();
    return entry;
}

} // namespace

bool SamePluginPath(const std::string& aUtf8, const std::string& bUtf8) {
    return rs::legacy_plugin::NormalizePluginFolderKey(aUtf8) == rs::legacy_plugin::NormalizePluginFolderKey(bUtf8);
}

bool PluginSelector::AddPath(const std::string& folderUtf8, std::string& errorOut) {
    rs::legacy_plugin::LoadOptions opt;
    opt.loadMeshes = false; // 種別・名前だけ分かればよく、本常駐はしない
    rs::legacy_plugin::LoadedPlugin lp =
        *GetParsedPluginCache().EnsureParsed(folderUtf8, nullptr, opt);
    if (!lp.ok) {
        errorOut = LoadFailureMessage(folderUtf8);
        return false;
    }

    const rs::legacy_pi::PluginType type = lp.def.header.type;
    const std::string displayName = DisplayNameOf(lp.def, folderUtf8);

    if (IsTrackPlugin(type)) {
        if (FindTrackPluginEntry(folderUtf8) != nullptr) {
            errorOut = "既にレール系リストに追加済みです: " + folderUtf8;
            return false;
        }
        TrackPluginEntry entry;
        entry.folderUtf8 = folderUtf8;
        entry.type = type;
        entry.displayName = displayName;
        entry.def = lp.def;
        AppendTrackPluginEntry(std::move(entry));
        return true;
    }

    if (type == rs::legacy_pi::PluginType::Train) {
        if (Formations().empty()) AddFormation("");
        FormationCar entry = MakeFormationCar(folderUtf8, displayName, lp.def);
        RestoreFormationCar(SelectedFormationIndex(), SelectedFormationCars().size(), std::move(entry));
        return true;
    }

    // それ以外(Station/Struct/Surface/Env/Skin/Unknown等)は種別ごとに1件のみ保持する。
    singleSlots_[type] = folderUtf8;
    return true;
}

std::optional<std::string> PluginSelector::EnsureTrackPluginLoaded(const std::string& folderUtf8,
                                                                  std::string& errorOut) {
    const TrackPluginEntry* existing = FindTrackPluginEntry(folderUtf8);
    if (existing != nullptr) return existing->folderUtf8;

    rs::legacy_plugin::LoadOptions opt;
    opt.loadMeshes = false;
    rs::legacy_plugin::LoadedPlugin lp =
        *GetParsedPluginCache().EnsureParsed(folderUtf8, nullptr, opt);
    if (!lp.ok) {
        errorOut = LoadFailureMessage(folderUtf8);
        return std::nullopt;
    }
    if (!IsTrackPlugin(lp.def.header.type)) {
        errorOut = "レール系(Rail/Tie/Girder/Pier/Line/Pole)以外の種別です: " + folderUtf8;
        return std::nullopt;
    }

    std::string displayName = lp.def.header.name;
    if (displayName.empty()) {
        std::filesystem::path fp(rs::platform_text::Utf8ToWide(folderUtf8));
        displayName = rs::platform_text::WideToUtf8(fp.filename().wstring());
    }

    TrackPluginEntry entry;
    entry.folderUtf8 = folderUtf8;
    entry.type = lp.def.header.type;
    entry.displayName = displayName;
    entry.def = lp.def;
    AppendTrackPluginEntry(std::move(entry));
    return trackPluginEntries_.back().folderUtf8;
}

std::string PluginSelector::RegisteredTrackPluginFolder(const std::string& folderUtf8) const {
    const TrackPluginEntry* entry = FindTrackPluginEntry(folderUtf8);
    return entry != nullptr ? entry->folderUtf8 : folderUtf8;
}

void PluginSelector::CanonicalizeTrackPluginFolders(rs::world::TrackEdge& edge) const {
    for (const std::string& folder : rs::world::ActiveFolders(edge)) {
        const std::string registered = RegisteredTrackPluginFolder(folder);
        if (folder != registered) EditFolderInEdge(edge, folder, &registered);
    }
    for (rs::world::ImportedPluginLink& link : edge.importedPiers)
        link.folderUtf8 = RegisteredTrackPluginFolder(link.folderUtf8);
    for (rs::world::ImportedPluginLink& link : edge.importedPoles)
        link.folderUtf8 = RegisteredTrackPluginFolder(link.folderUtf8);
    edge.importedPierFolderUtf8 = RegisteredTrackPluginFolder(edge.importedPierFolderUtf8);
    edge.importedPoleFolderUtf8 = RegisteredTrackPluginFolder(edge.importedPoleFolderUtf8);
}

void PluginSelector::AppendTrackPluginEntry(TrackPluginEntry&& entry) {
    const std::string normalized = rs::legacy_plugin::NormalizePluginFolderKey(entry.folderUtf8);
    trackPluginEntries_.push_back(std::move(entry));
    normalizedIndex_[normalized] = trackPluginEntries_.size() - 1;
    rawLookupCache_.clear(); // 以前「無い」と答えた綴りが今回追加されている可能性がある
    ++entryRevision_;
}

const TrackPluginEntry* PluginSelector::FindTrackPluginEntry(const std::string& folderUtf8) const {
    const std::unordered_map<std::string, size_t>::const_iterator cached =
        rawLookupCache_.find(folderUtf8);
    if (cached != rawLookupCache_.end())
        return cached->second == kNotFound ? nullptr : &trackPluginEntries_[cached->second];

    const std::unordered_map<std::string, size_t>::const_iterator found =
        normalizedIndex_.find(rs::legacy_plugin::NormalizePluginFolderKey(folderUtf8));
    const size_t index = found == normalizedIndex_.end() ? kNotFound : found->second;
    rawLookupCache_.emplace(folderUtf8, index);
    return index == kNotFound ? nullptr : &trackPluginEntries_[index];
}

void PluginSelector::AddUnresolvedFormationCar(size_t defIndex, const std::string& folderUtf8) {
    if (defIndex >= Formations().size()) return;
    FormationCar entry;
    entry.folderUtf8 = folderUtf8;
    const std::filesystem::path folderPath(rs::platform_text::Utf8ToWide(folderUtf8));
    entry.displayName = rs::platform_text::WideToUtf8(folderPath.filename().wstring());
    entry.unresolved = true;
    RestoreFormationCar(defIndex, Formations()[defIndex].cars.size(), std::move(entry));
}

bool PluginSelector::LoadFormationCar(const std::string& folderUtf8, FormationCar& carOut,
                                      std::string& errorOut) const {
    rs::legacy_plugin::LoadOptions opt;
    opt.loadMeshes = false;
    const rs::legacy_plugin::LoadedPlugin lp =
        *GetParsedPluginCache().EnsureParsed(folderUtf8, nullptr, opt);
    if (!lp.ok) {
        errorOut = LoadFailureMessage(folderUtf8);
        return false;
    }
    if (lp.def.header.type != rs::legacy_pi::PluginType::Train) {
        errorOut = "列車プラグインではありません: " + folderUtf8;
        return false;
    }
    carOut = MakeFormationCar(folderUtf8, DisplayNameOf(lp.def, folderUtf8), lp.def);
    return true;
}

bool PluginSelector::InsertFormationCar(size_t defIndex, size_t position, const std::string& folderUtf8,
                                        std::string& errorOut) {
    if (defIndex >= Formations().size() || position > Formations()[defIndex].cars.size()) {
        errorOut = "挿入先の編成か位置が範囲外です";
        return false;
    }
    FormationCar car;
    if (!LoadFormationCar(folderUtf8, car, errorOut)) return false;
    RestoreFormationCar(defIndex, position, std::move(car));
    return true;
}

void PluginSelector::RemoveSingleSlot(rs::legacy_pi::PluginType type) {
    singleSlots_.erase(type);
}

} // namespace rs::app
