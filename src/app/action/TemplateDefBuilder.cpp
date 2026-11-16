#include "TemplateDefBuilder.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <utility>

#include "../../platform/text/Encoding.h"

namespace rs::app {

namespace {

using rs::legacy_pi::PluginType;

std::filesystem::path Utf8Path(const std::string& utf8) {
    return std::filesystem::path(rs::platform_text::Utf8ToWide(utf8));
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return std::ranges::equal(a, b, [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

std::string NameFromComponents(const std::vector<std::filesystem::path>& parts, std::size_t first) {
    std::filesystem::path name;
    for (std::size_t i = first; i < parts.size(); ++i) name /= parts[i];
    return rs::platform_text::WideToUtf8(name.wstring());
}

// 種類ごとの先頭を専用の欄へ入れるための対応表(RailwayPluginSetDefの欄の並び順)。
struct SlotRef {
    PluginType type;
    std::string rs::legacy_layout::RailwayPluginSetDef::*plugin;
    bool rs::legacy_layout::RailwayPluginSetDef::*check;
};

constexpr std::array<SlotRef, 6> kSlots{{
    {PluginType::Rail, &rs::legacy_layout::RailwayPluginSetDef::railPlugin, &rs::legacy_layout::RailwayPluginSetDef::railCheck},
    {PluginType::Tie, &rs::legacy_layout::RailwayPluginSetDef::tiePlugin, &rs::legacy_layout::RailwayPluginSetDef::tieCheck},
    {PluginType::Girder, &rs::legacy_layout::RailwayPluginSetDef::girderPlugin, &rs::legacy_layout::RailwayPluginSetDef::girderCheck},
    {PluginType::Pier, &rs::legacy_layout::RailwayPluginSetDef::pierPlugin, &rs::legacy_layout::RailwayPluginSetDef::pierCheck},
    {PluginType::Line, &rs::legacy_layout::RailwayPluginSetDef::linePlugin, &rs::legacy_layout::RailwayPluginSetDef::lineCheck},
    {PluginType::Pole, &rs::legacy_layout::RailwayPluginSetDef::polePlugin, &rs::legacy_layout::RailwayPluginSetDef::poleCheck},
}};

}  // namespace

std::string PluginNameUnderKind(const std::string& folderUtf8, const char* kindDir,
                                const std::vector<std::string>& rootsUtf8) {
    const std::filesystem::path folder = Utf8Path(folderUtf8).lexically_normal();
    for (const std::string& rootUtf8 : rootsUtf8) {
        const std::filesystem::path relative = folder.lexically_relative(Utf8Path(rootUtf8) / kindDir);
        if (relative.empty() || *relative.begin() == "..") continue;
        return rs::platform_text::WideToUtf8(relative.wstring());
    }
    std::vector<std::filesystem::path> parts(folder.begin(), folder.end());
    while (!parts.empty() && parts.back().empty()) parts.pop_back();
    const std::wstring kind = Utf8Path(kindDir).wstring();
    for (std::size_t i = parts.size(); i-- > 1;) {
        if (EqualsIgnoreCase(parts[i - 1].wstring(), kind)) return NameFromComponents(parts, i);
    }
    return parts.empty() ? std::string() : rs::platform_text::WideToUtf8(parts.back().wstring());
}

rs::legacy_layout::RailwayPluginSetDef BuildRailwayPluginSetDef(
    const TrackPluginSetViewModel& set, const rs::editor::TrackLayingSettings& settings,
    const std::string& settingName, const std::vector<std::string>& rootsUtf8) {
    rs::legacy_layout::RailwayPluginSetDef def;
    def.found = true;
    def.settingName = settingName;
    for (const TrackPluginSetItem& item : set.items) {
        if (!item.loaded) continue;
        const std::array<SlotRef, 6>::const_iterator slot = std::ranges::find(kSlots, item.type, &SlotRef::type);
        if (slot == kSlots.end()) continue;
        const std::string name = PluginNameUnderKind(item.folderUtf8, rs::legacy_pi::PluginTypeName(item.type), rootsUtf8);
        if (name.empty()) continue;
        if (def.*(slot->check)) {
            def.extraPlugins.push_back(name);
        } else {
            def.*(slot->plugin) = name;
            def.*(slot->check) = true;
        }
    }
    def.multiTrack = settings.trackCount > 1;
    def.trackNum = settings.trackCount;
    def.trackInterval = settings.trackSpacing;
    return def;
}

rs::legacy_layout::TrainGroupTemplateDef BuildTrainGroupTemplateDef(
    const Formation& formation, const std::vector<std::string>& rootsUtf8) {
    rs::legacy_layout::TrainGroupTemplateDef def;
    def.found = true;
    for (const FormationCar& car : formation.cars) {
        rs::legacy_layout::TrainTemplateEntry entry;
        entry.trainPlugin = PluginNameUnderKind(car.folderUtf8, "Train", rootsUtf8);
        entry.reverse = car.reversed;
        entry.switchOptions = car.switchOptions;
        def.trains.push_back(std::move(entry));
    }
    return def;
}

} // namespace rs::app
