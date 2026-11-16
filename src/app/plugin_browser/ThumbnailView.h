//
// ThumbnailView.h - サムネイルの撮り方の変種と、画像のストアのキー
//
// 変種は車両(Train)を編成カードに出すときだけ使う。ブラウザのカードは Standard のまま。
// カメラの角度と合わせ方は表(GetThumbnailView)に集め、ThumbnailViewCamera が表から位置を決める。
// 方位は +Z 軸から +X 軸へ回した角度で、車体の前面は +Z 側(反転した車両は 180 度回って前面が -Z 側になる)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>

#include "../workspace/CatalogCheck.h"
#include "../workspace/EnumIndex.h"
#include "PluginCatalog.h"

namespace rs::app::plugin_browser {

enum class ThumbnailView {
    Standard,           // 斜め前から見下ろす(ブラウザのカード)
    Side,               // 真横(-X 側の側面)から水平に(編成カードの向きどおりの車両。画面の左が前面)
    SideOpposite,       // Side の反対側(+X 側の側面)から(編成カードの反転した車両。画面の右が前面)
    FrontFace,          // 前面(+Z 側の端)を正面から(編成カードの先頭アイコン)
    FrontFaceOpposite,  // 後面(-Z 側の端)を正面から(先頭が反転しているときの先頭アイコン。反転車両の前面)
    LayingSet,          // 敷設プラグインセットの合成(Rail・Tie・Girder・Line を重ねた直線。斜め上から縦長に)
    TrainTemplate,      // 編成テンプレートのカード(編成の全車両を直線に並べ、浅い角度の斜め前から)
    RailTemplate,       // 敷設テンプレートのカード(LayingSet と同じ合成を、ブラウザのカードの4:3で)
};

inline constexpr std::size_t kThumbnailViewCount = 8;

// 何を画面に収めるか。
enum class ThumbnailFit {
    BoundingSphere,  // 外接球
    BoundingBox,     // AABBの8隅
    SideFace,        // 車体の側面の矩形(長さ Z × 高さ Y)
    EndFace,         // 前後どちらかの端面の断面
};

// 撮影解像度。ブラウザのカード用は 4:3。
inline constexpr std::uint32_t kCardThumbnailWidth = 256;
inline constexpr std::uint32_t kCardThumbnailHeight = 192;
// 側面は横長。鉄道車両の車体は長さ約20m・高さ約4m(屋根上機器を含む)で約5:1だが、
// 画面が細くなりすぎて編成カードの箱で小さく見えるため、4:1 に余白を持たせて収める。
inline constexpr std::uint32_t kSideThumbnailWidth = 384;
inline constexpr std::uint32_t kSideThumbnailHeight = 96;
// 先頭車アイコンは正方形(アイコンの箱が正方形のため)。
inline constexpr std::uint32_t kFrontIconThumbnailSize = 128;
// 敷設プラグインセットの合成は縦長(2:3)。
inline constexpr std::uint32_t kLayingSetThumbnailWidth = 320;
inline constexpr std::uint32_t kLayingSetThumbnailHeight = 480;

struct ThumbnailViewTraits {
    ThumbnailView view;
    char sourceTag;          // 画像のソース名に入れる1文字
    float azimuthDeg;        // 方位
    float elevationDeg;      // 俯角
    ThumbnailFit fit;
    float fovYDeg;           // 縦の視野角。狭いほど遠近の歪みが減り、カメラは遠くなる
    float fitMargin;         // 収めるものに掛ける余白の倍率(1.1=10%)。BoundingBox は表によらず ThumbnailViewCamera の値
    std::uint32_t width;     // 撮影解像度
    std::uint32_t height;
};

inline constexpr ThumbnailViewTraits kThumbnailViews[] = {
    {ThumbnailView::Standard, 's', 30.0f, 8.0f, ThumbnailFit::BoundingBox, 15.0f, 1.1f, kCardThumbnailWidth,
     kCardThumbnailHeight},
    {ThumbnailView::Side, 'l', 270.0f, 0.0f, ThumbnailFit::SideFace, 8.0f, 1.1f, kSideThumbnailWidth,
     kSideThumbnailHeight},
    {ThumbnailView::SideOpposite, 'o', 90.0f, 0.0f, ThumbnailFit::SideFace, 8.0f, 1.1f, kSideThumbnailWidth,
     kSideThumbnailHeight},
    {ThumbnailView::FrontFace, 'f', 0.0f, 6.0f, ThumbnailFit::EndFace, 8.0f, 1.03f, kFrontIconThumbnailSize,
     kFrontIconThumbnailSize},
    {ThumbnailView::FrontFaceOpposite, 'r', 180.0f, 6.0f, ThumbnailFit::EndFace, 8.0f, 1.03f,
     kFrontIconThumbnailSize, kFrontIconThumbnailSize},
    {ThumbnailView::LayingSet, 'g', 165.0f, 15.0f, ThumbnailFit::BoundingBox, 15.0f, 1.1f, kLayingSetThumbnailWidth,
     kLayingSetThumbnailHeight},
    // 角度と視野角は仮の値(実機で決める)。編成は延長線に対して浅い角度の斜め前から、狭い視野角で撮る。
    {ThumbnailView::TrainTemplate, 't', 14.0f, 6.0f, ThumbnailFit::BoundingBox, 10.0f, 1.1f, kCardThumbnailWidth,
     kCardThumbnailHeight},
    {ThumbnailView::RailTemplate, 'p', 165.0f, 15.0f, ThumbnailFit::BoundingBox, 15.0f, 1.1f, kCardThumbnailWidth,
     kCardThumbnailHeight},
};

static_assert(std::size(kThumbnailViews) == kThumbnailViewCount, "kThumbnailViewsの行数がThumbnailViewと一致しない");
static_assert(rs::app::workspace::IsCatalogInEnumOrder(kThumbnailViews, &ThumbnailViewTraits::view),
              "kThumbnailViewsの並びがThumbnailViewの列挙順と一致しない");

constexpr const ThumbnailViewTraits& GetThumbnailView(ThumbnailView view) {
    return kThumbnailViews[rs::app::workspace::EnumIndex(view)];
}

// 画像のストアのキー。同じプラグインでも変種ごとに別の画像になる。
struct ImageKey {
    EntryId id = 0;
    ThumbnailView view = ThumbnailView::Standard;

    ImageKey() = default;
    // ブラウザのカード(Standard)は添字だけで書けるようにしておく。
    ImageKey(EntryId entryId, ThumbnailView thumbnailView = ThumbnailView::Standard)
        : id(entryId), view(thumbnailView) {}
    bool operator==(const ImageKey&) const = default;
};

} // namespace rs::app::plugin_browser

template <>
struct std::hash<rs::app::plugin_browser::ImageKey> {
    std::size_t operator()(const rs::app::plugin_browser::ImageKey& key) const noexcept {
        return std::hash<std::size_t>()(key.id) * 31u + static_cast<std::size_t>(key.view);
    }
};
