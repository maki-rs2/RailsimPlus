//
// TemplateCardInfo.h - テンプレートのカード1枚ぶんの重い情報(両数・警告・画像のキー)と、その結果の置き場
//
// RmlUi非依存。カードの両数・見つからない車両の数・画像のキーは、どれも編成テンプレートを1回パースして求まるので、
// まとめて計算する(同じファイルを2回パースしない)。ファイルを読むので、描画スレッドでは呼ばず、ワーカーで計算する。
//
#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "../plugin_session/TrainGroupTemplateParse.h"
#include "TemplateImageStore.h"

namespace rs::app::plugin_browser {

struct TemplateCardInfo {
    bool hasSummary = false; // 両数と警告を持つか(編成だけ)
    std::size_t carCount = 0;
    std::size_t missingCount = 0;
    TemplateImageKey imageKey = 0;
};

// 編成テンプレートのパース関数(テストで差し替えられるよう外から渡す)。
using TemplateParseFunc =
    std::function<TrainGroupTemplateParseResult(const std::string& pathUtf8, const std::string& rootUtf8)>;

// 編成は、1回のパースから両数・警告・画像のキーを求める。敷設は画像のキーだけ。
TemplateCardInfo ComputeTemplateCardInfo(const TemplateImageSource& source, const TemplateParseFunc& parse);

// 結果の使い回しの識別子。ファイルの更新時刻とプラグインの読込の世代が同じなら、同じ結果になる。
struct TemplateCardInfoCacheKey {
    TemplateImageKind kind = TemplateImageKind::Train;
    std::string pathUtf8;
    std::string rootUtf8;
    std::int64_t writeTime = 0; // ファイルが読めないときは0
    std::uint64_t generation = 0;
    std::strong_ordering operator<=>(const TemplateCardInfoCacheKey&) const = default;
};

TemplateCardInfoCacheKey MakeTemplateCardInfoCacheKey(const TemplateImageSource& source, std::uint64_t generation);

// 計算結果の置き場。複数のスレッドから使える。件数が上限を超えたら全部捨てる(一覧は数百件なので、上限には普通届かない)。
class TemplateCardInfoCache {
public:
    explicit TemplateCardInfoCache(std::size_t maxEntries = 4096) : maxEntries_(maxEntries) {}

    std::optional<TemplateCardInfo> Find(const TemplateCardInfoCacheKey& key) const;
    void Store(const TemplateCardInfoCacheKey& key, const TemplateCardInfo& info);
    std::size_t Size() const;

private:
    std::size_t maxEntries_;
    mutable std::mutex mutex_;
    std::map<TemplateCardInfoCacheKey, TemplateCardInfo> entries_;
};

} // namespace rs::app::plugin_browser
