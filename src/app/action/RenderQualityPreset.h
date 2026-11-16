//
// RenderQualityPreset.h - 描画設定の画質プリセット(低・中・高)
//
// プリセットが決めるのは、画質に直結する項目(大気遠近・高さフォグ・影の品質と距離・レンズフレア3種・
// ブルーム・ボリュメトリックフォグ・アンチエイリアス・AO)だけ。描画距離・VSync・FPS上限・空の選択・
// トーンマップ・モーションブラーは、好みや環境で選ぶものなのでプリセットに含めない(適用しても変えず、
// 一致の判定にも使わない)。値は assets/config/render_quality_presets.json(開発側が決める調整値)から読む。
//
// 一致の判定と適用は、設定の値の選択肢の添字(RenderSettingsViewModel)で行う。読めない保存値も、
// 画面に出ている値と同じ選択肢になる。
//
#pragma once

#include <array>
#include <optional>
#include <string_view>

#include "RenderSettingsAction.h"

namespace rs::app {

// プリセットの保存・画面上のキー。並びは低・中・高。
inline constexpr std::array<std::string_view, 3> kRenderQualityPresetKeys = {"low", "medium", "high"};
// どのプリセットとも一致しないときの画面上のキー(選べるプリセットではない)。
inline constexpr std::string_view kCustomQualityPresetKey = "custom";

struct RenderQualityValues {
    bool atmosphericPerspective = false;
    bool heightFog = false;
    int shadowQualityIndex = 0;    // kShadowQualityChoicesの添字
    int shadowDistanceIndex = 0;   // kShadowDistanceChoicesの添字
    bool sunLensFlare = false;
    bool sunWhiteout = false;
    bool miscLensFlare = false;
    bool bloom = false;
    bool volumetricFog = false;
    int antiAliasingIndex = 0;     // kAntiAliasingChoicesの添字
    int ambientOcclusionIndex = 0; // kAoQualityChoicesの添字
    bool operator==(const RenderQualityValues&) const = default;
};

struct RenderQualityPresets {
    // kRenderQualityPresetKeysと同じ並び。
    std::array<RenderQualityValues, kRenderQualityPresetKeys.size()> values;

    // JSON(low・medium・highをキーに持つオブジェクト)を読む。値の文字列は保存用のキーで、
    // 選択肢に無いキーや足りない項目があれば読めない扱い(nullopt)。
    static std::optional<RenderQualityPresets> FromJson(std::string_view json);
    // 埋め込みの調整値。読めないときは警告を出して、すべて既定の値(全項目オフ・先頭の選択肢)にする。
    static const RenderQualityPresets& Embedded();
};

// いまの設定のうち、プリセットが決める項目。
RenderQualityValues QualityValuesOf(const RenderSettingsViewModel& viewModel);

// viewModelがどのプリセットと一致するか(そのキー)。どれとも一致しなければkCustomQualityPresetKey。
std::string_view MatchRenderQualityPreset(const RenderQualityPresets& presets, const RenderSettingsViewModel& viewModel);

// keyのプリセットをactionのSetterで適用する(変わる項目だけ)。keyがプリセットでなければfalseで何もしない。
bool ApplyRenderQualityPreset(RenderSettingsAction& action, const RenderSettingsViewModel& current,
                              const RenderQualityPresets& presets, std::string_view key);

} // namespace rs::app
