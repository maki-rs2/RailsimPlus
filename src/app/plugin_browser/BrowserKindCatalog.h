//
// BrowserKindCatalog.h - プラグインブラウザの種別(タブ・走査の単位)を1つの表にまとめる
//
// 種別ごとの違い(PluginType・種別フォルダ名・
// タブの表示名)はこの表だけで持ち、ほかのコードに種別名での分岐を書かない。
//
#pragma once

#include <array>
#include <cstddef>
#include <iterator>
#include <optional>


#include "../../plugins/schema/PluginTypeTraits.h"
#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"
#include "BrowserTuning.h"
#include "../../plugins/schema/PluginHeader.h"

namespace rs::app::plugin_browser {

// Signalは走査の単位ではない。Stationの走査結果のうちhasSignalが真のものをSignalタブに出す。
enum class BrowserKind { Rail, Tie, Girder, Pier, Line, Pole, Station, Struct, Signal, Train };

inline constexpr std::size_t kBrowserKindCount = magic_enum::enum_count<BrowserKind>();

// サムネイルのメッシュの作り方。
using ThumbnailMethod = rs::legacy_pi::PluginMeshMethod;

struct BrowserKindTraits {
    BrowserKind kind;
    rs::legacy_pi::PluginType pluginType;
    const char* folderName;   // ルート直下の種別フォルダ名
    const char* displayName;  // タブの表示名
    bool signalOnly;          // 真なら走査せず、Stationの結果から絞り込んで出す
    ThumbnailMethod thumbnailMethod; // サムネイルのメッシュの作り方
    // Modelでは直線を使わないのでnullptr。それ以外は調整値の対応する長さを引く。
    float BrowserTuning::*thumbnailLength;

    float ThumbnailTrackLengthM() const {
        return thumbnailLength == nullptr ? 0.0f : BrowserTuning::Embedded().*thumbnailLength;
    }
    float ViewerTrackLengthM() const {
        return thumbnailLength == nullptr ? 0.0f : BrowserTuning::Embedded().viewerTrackLengthM;
    }

    constexpr BrowserKindTraits(BrowserKind browserKind, rs::legacy_pi::PluginType type,
                                 const char* name, bool onlySignal, float BrowserTuning::*length)
        : kind(browserKind), pluginType(type),
          folderName(rs::legacy_pi::GetPluginTypeTraits(type).folderName), displayName(name),
          signalOnly(onlySignal), thumbnailMethod(rs::legacy_pi::GetPluginTypeTraits(type).meshMethod),
          thumbnailLength(length) {}
};

inline constexpr BrowserKindTraits kBrowserKinds[] = {
    {BrowserKind::Rail, rs::legacy_pi::PluginType::Rail, "レール", false, &BrowserTuning::railThumbnailLengthM},
    {BrowserKind::Tie, rs::legacy_pi::PluginType::Tie, "枕木", false, &BrowserTuning::railThumbnailLengthM},
    {BrowserKind::Girder, rs::legacy_pi::PluginType::Girder, "桁", false, &BrowserTuning::girderThumbnailLengthM},
    {BrowserKind::Pier, rs::legacy_pi::PluginType::Pier, "橋脚", false, nullptr},
    {BrowserKind::Line, rs::legacy_pi::PluginType::Line, "架線", false, &BrowserTuning::lineThumbnailLengthM},
    {BrowserKind::Pole, rs::legacy_pi::PluginType::Pole, "架線柱", false, nullptr},
    {BrowserKind::Station, rs::legacy_pi::PluginType::Station, "駅", false, nullptr},
    {BrowserKind::Struct, rs::legacy_pi::PluginType::Struct, "建造物", false, nullptr},
    {BrowserKind::Signal, rs::legacy_pi::PluginType::Station, "信号機", true, nullptr},
    {BrowserKind::Train, rs::legacy_pi::PluginType::Train, "列車", false, nullptr},
};

static_assert(std::size(kBrowserKinds) == kBrowserKindCount, "kBrowserKindsの行数がBrowserKindと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kBrowserKinds, &BrowserKindTraits::kind),
              "kBrowserKindsの並びがBrowserKindの列挙順と一致しない");

constexpr const BrowserKindTraits& GetBrowserKind(BrowserKind kind) {
    return kBrowserKinds[rs::app::workspace::EnumIndex(kind)];
}

// 走査対象(signalOnly=falseの行)の数。
constexpr std::size_t CountScanKinds() {
    std::size_t count = 0;
    for (const BrowserKindTraits& row : kBrowserKinds) {
        if (!row.signalOnly) ++count;
    }
    return count;
}

inline constexpr std::size_t kScanKindCount = CountScanKinds();

// 走査対象の種別を表の順に並べたもの。
constexpr std::array<BrowserKind, kScanKindCount> GetScanKinds() {
    std::array<BrowserKind, kScanKindCount> kinds{};
    std::size_t next = 0;
    for (const BrowserKindTraits& row : kBrowserKinds) {
        if (!row.signalOnly) kinds[next++] = row.kind;
    }
    return kinds;
}

// ヘッダのPluginTypeから走査対象の種別を引く。Signalは返さない(Stationとして返す)。
// 表にない種別(Env等)はnullopt。
constexpr std::optional<BrowserKind> BrowserKindFromPluginType(rs::legacy_pi::PluginType type) {
    for (const BrowserKindTraits& row : kBrowserKinds) {
        if (!row.signalOnly && row.pluginType == type) return row.kind;
    }
    return std::nullopt;
}

} // namespace rs::app::plugin_browser
