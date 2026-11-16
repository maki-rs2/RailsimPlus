#include "MissingPluginViewModel.h"

#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>

#include <magic_enum/magic_enum.hpp>

#include "../plugin_session/DefaultPluginCatalog.h"
#include "../workspace/CatalogCheck.h"

namespace rs::app {
namespace {

using rs::legacy_pi::PluginType;

// 場所の種類ごとの違い。列挙順に1行ずつ並べる。
struct SiteViewTraits {
    PluginReferenceSite site;
    const char* kindKey;    // 種類名の翻訳キー
    PluginType folderType;  // フォルダ選択を最初に開く、ルート直下の種別フォルダ(kDefaultPluginKindsの行)
};

constexpr SiteViewTraits kSiteViewTraits[] = {
    {PluginReferenceSite::SceneryEntity, "missing_plugin.site.scenery", PluginType::Struct},
    {PluginReferenceSite::CarEntity, "missing_plugin.site.car", PluginType::Train},
    {PluginReferenceSite::TrackSection, "missing_plugin.site.track_section", PluginType::Rail},
    {PluginReferenceSite::Signal, "missing_plugin.site.signal", PluginType::Station},
    {PluginReferenceSite::PlacedIndividual, "missing_plugin.site.placed_individual", PluginType::Pole},
    {PluginReferenceSite::WireLine, "missing_plugin.site.wire_line", PluginType::Line},
    {PluginReferenceSite::FormationCar, "missing_plugin.site.formation_car", PluginType::Train},
    {PluginReferenceSite::Environment, "missing_plugin.site.environment", PluginType::Env},
};

static_assert(std::size(kSiteViewTraits) == magic_enum::enum_count<PluginReferenceSite>(),
              "kSiteViewTraitsの行数がPluginReferenceSiteと一致しない");
static_assert(workspace::IsCatalogInEnumOrder(kSiteViewTraits, &SiteViewTraits::site),
              "kSiteViewTraitsの並びがPluginReferenceSiteの列挙順と一致しない");

const SiteViewTraits& TraitsOf(PluginReferenceSite site) {
    return kSiteViewTraits[workspace::EnumIndex(site)];
}

std::string ReplaceAll(std::string text, std::string_view from, const std::string& to) {
    for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

const char* NoticeKey(MissingPluginReplaceStatus status, bool wasDelete) {
    switch (status) {
    case MissingPluginReplaceStatus::Ok:
        return wasDelete ? "missing_plugin.notice.deleted" : "missing_plugin.notice.replaced";
    case MissingPluginReplaceStatus::NothingToReplace: return "missing_plugin.notice.nothing_to_replace";
    case MissingPluginReplaceStatus::SameAsSource: return "missing_plugin.notice.same_as_source";
    case MissingPluginReplaceStatus::LoadFailed: return "missing_plugin.notice.load_failed";
    case MissingPluginReplaceStatus::TypeMismatch: return "missing_plugin.notice.type_mismatch";
    case MissingPluginReplaceStatus::EnvOutsideRoots: return "missing_plugin.notice.env_outside_roots";
    case MissingPluginReplaceStatus::NotDeletable: return "missing_plugin.notice.not_deletable";
    }
    return "missing_plugin.notice.load_failed";
}

}  // namespace

std::string ExpandMissingPluginText(const std::string& text, std::size_t count, const std::string& detail) {
    return ReplaceAll(ReplaceAll(text, "{count}", std::to_string(count)), "{detail}", detail);
}

std::string MissingPluginKindName(PluginReferenceSite site, const Localization& localization) {
    return localization.Text(TraitsOf(site).kindKey);
}

std::vector<MissingPluginRowView> BuildMissingPluginRows(const std::vector<MissingPluginEntry>& entries,
                                                         const Localization& localization) {
    const std::string usageFormat = localization.Text("missing_plugin.usage");
    std::vector<MissingPluginRowView> rows;
    rows.reserve(entries.size());
    for (const MissingPluginEntry& entry : entries) {
        MissingPluginRowView row;
        row.name = PluginFolderLeafName(entry.folderUtf8);
        // 名前が取れない表記(区切りだけなど)でも、行が空にならないようにパスをそのまま見せる。
        if (row.name.empty()) row.name = entry.folderUtf8;
        row.path = entry.folderUtf8;
        row.kind = MissingPluginKindName(entry.site, localization);
        row.usage = ExpandMissingPluginText(usageFormat, entry.usageCount, {});
        row.deletable = entry.site != PluginReferenceSite::Environment;
        rows.push_back(std::move(row));
    }
    return rows;
}

MissingPluginTexts BuildMissingPluginTexts(const Localization& localization) {
    MissingPluginTexts texts;
    texts.title = localization.Text("missing_plugin.title");
    texts.badgeTitle = localization.Text("missing_plugin.badge_title");
    texts.addRoot = localization.Text("missing_plugin.add_root");
    texts.rescan = localization.Text("missing_plugin.rescan");
    texts.replace = localization.Text("missing_plugin.replace");
    texts.remove = localization.Text("missing_plugin.delete");
    return texts;
}

std::size_t MissingPluginBadgeCount(const std::vector<MissingPluginEntry>& entries) {
    return entries.size();
}

MissingPluginNotice BuildMissingPluginNotice(const MissingPluginReplaceResult& result, bool wasDelete,
                                             const Localization& localization) {
    MissingPluginNotice notice;
    notice.warning = !result.Ok();
    notice.text = ExpandMissingPluginText(localization.Text(NoticeKey(result.status, wasDelete)),
                                          result.replacedCount, result.detail);
    return notice;
}

std::string MissingPluginPickerStartFolder(PluginReferenceSite site, const std::vector<std::string>& roots,
                                           const PluginExistsFn& folderExists) {
    if (roots.empty()) return {};
    const std::size_t typeIndex = static_cast<std::size_t>(TraitsOf(site).folderType);
    const std::string kindFolder = kDefaultPluginKinds[typeIndex - 1].folderName;
    for (const std::string& root : roots) {
        std::string candidate = root;
        while (!candidate.empty() && (candidate.back() == '/' || candidate.back() == '\\')) candidate.pop_back();
        candidate += "/" + kindFolder;
        if (folderExists(candidate)) return candidate;
    }
    return roots.front();
}

}  // namespace rs::app
