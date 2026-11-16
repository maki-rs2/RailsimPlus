//
// RenderRebuild.h - 画面の大きさや設定の変更で作り直す資源を、dirty フラグの表でまとめる(GPUに依存しない)
//
// 変更の要求は、フラグを立てるだけ(要求値は設定側へ即座に入れる)。資源の作り直しは App::Render の先頭などで
// 1か所にまとめて行い、GPU の完了待ちも1回で済ませる。フラグごとの作り直しの中身と実行順は kRebuildTable が決める。
// 同じ作り直しを複数のフラグが指すときは、1回だけ行う。
//
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rs::app {

enum class RenderRebuild : std::uint32_t {
    None = 0,
    Resize = 1u << 0,           // 画面の大きさが変わった
    ViewerBackground = 1u << 1, // ビューアーの背景色が変わった(HDRシーンの消去値)
    AoQuality = 1u << 2,        // アンビエントオクルージョンの品質が変わった
    TemporalAa = 1u << 3,       // TAA の有無が変わった
    MotionBlur = 1u << 4,       // モーションブラーの有無が変わった
    VolumetricFog = 1u << 5,    // ボリュメトリックフォグの有無が変わった
    ShadowMap = 1u << 6,        // シャドウマップの大きさ・枚数が変わった
    DepthOfField = 1u << 7,     // フォトモードの被写界深度の有無(ファインダーが出ているか)が変わった
    LuminanceMeter = 1u << 8,   // フォトモードの ISO オートの測光の有無が変わった
};

constexpr RenderRebuild operator|(RenderRebuild a, RenderRebuild b) {
    return static_cast<RenderRebuild>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr RenderRebuild operator&(RenderRebuild a, RenderRebuild b) {
    return static_cast<RenderRebuild>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}

// 作り直しの中身。
enum class RebuildAction : std::uint8_t {
    Swapchain,     // スワップチェーンのバッファ、深度・WBOIT・HDRシーン・ポストプロセスの大きさ
    HdrClearColor, // HDRシーンの消去値
    SizedPasses,   // 深度プリパス・速度・AO・TAA・モーションブラー・被写界深度・測光・フォグの資源
    ShadowMap,     // シャドウマップ
};

struct RebuildEntry {
    RenderRebuild flag;
    RebuildAction action;
};

// 実行順。大きさの変更が先(後ろの作り直しは新しい大きさで作る)。
inline constexpr std::array<RebuildEntry, 9> kRebuildTable = {{
    {RenderRebuild::Resize, RebuildAction::Swapchain},
    {RenderRebuild::ViewerBackground, RebuildAction::HdrClearColor},
    {RenderRebuild::AoQuality, RebuildAction::SizedPasses},
    {RenderRebuild::TemporalAa, RebuildAction::SizedPasses},
    {RenderRebuild::MotionBlur, RebuildAction::SizedPasses},
    {RenderRebuild::VolumetricFog, RebuildAction::SizedPasses},
    {RenderRebuild::DepthOfField, RebuildAction::SizedPasses},
    {RenderRebuild::LuminanceMeter, RebuildAction::SizedPasses},
    {RenderRebuild::ShadowMap, RebuildAction::ShadowMap},
}};

inline constexpr std::size_t kRebuildActionCount = 4;

// 立っているフラグに対する作り直しを、表の順に重複なく並べたもの。
struct RebuildPlan {
    std::array<RebuildAction, kRebuildActionCount> actions{};
    std::size_t count = 0;

    constexpr bool Has(RebuildAction action) const {
        for (std::size_t i = 0; i < count; ++i) {
            if (actions[i] == action) return true;
        }
        return false;
    }
};

constexpr RebuildPlan PlanRebuild(RenderRebuild dirty) {
    RebuildPlan plan;
    for (const RebuildEntry& entry : kRebuildTable) {
        if ((dirty & entry.flag) == RenderRebuild::None || plan.Has(entry.action)) continue;
        plan.actions[plan.count++] = entry.action;
    }
    return plan;
}

// 立てたフラグを溜め、まとめて取り出す。
class RenderDirty {
public:
    void Mark(RenderRebuild flag) { bits_ = bits_ | flag; }
    bool Any() const { return bits_ != RenderRebuild::None; }
    bool Has(RenderRebuild flag) const { return (bits_ & flag) != RenderRebuild::None; }
    // 溜まったフラグを返して空にする。
    RenderRebuild Take() {
        const RenderRebuild taken = bits_;
        bits_ = RenderRebuild::None;
        return taken;
    }

private:
    RenderRebuild bits_ = RenderRebuild::None;
};

} // namespace rs::app
