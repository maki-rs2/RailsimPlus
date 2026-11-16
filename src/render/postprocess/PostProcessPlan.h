//
// PostProcessPlan.h - ポストプロセスの設定と、実際に走らせる段の計画(GPUに依存しない純関数)
//
// チェーンの段(ブルーム・FXAA)を走らせる条件はこのファイルの PlanStages の1か所にだけ書く。
//
#pragma once

#include <array>
#include <string_view>

#include "TonemapMath.h"

namespace rs::render {

// アンチエイリアスの方式。
enum class AntiAliasing {
    None,
    Fxaa,
    Taa,
};

// ユーザー設定(settings.dat の aa)で選べる方式と、保存する文字列。画面の選択肢はこの表の並びと同じ。
struct AntiAliasingChoice {
    AntiAliasing aa;
    const char* key;
};

constexpr std::array<AntiAliasingChoice, 3> kAntiAliasingChoices = {{
    {AntiAliasing::None, "none"},
    {AntiAliasing::Fxaa, "fxaa"},
    {AntiAliasing::Taa, "taa"},
}};

constexpr AntiAliasing kDefaultAntiAliasing = AntiAliasing::Fxaa;

// 保存した文字列を方式へ変える。読めない文字列は既定。
constexpr AntiAliasing AntiAliasingFromKey(std::string_view key) {
    for (const AntiAliasingChoice& choice : kAntiAliasingChoices) {
        if (key == choice.key) return choice.aa;
    }
    return kDefaultAntiAliasing;
}

// 方式を保存する文字列へ変える。
constexpr const char* AntiAliasingKey(AntiAliasing aa) {
    const char* key = kAntiAliasingChoices.front().key;
    for (const AntiAliasingChoice& choice : kAntiAliasingChoices) {
        if (choice.aa == aa) key = choice.key;
    }
    return key;
}

// ユーザー設定(settings.dat)から来るスイッチ。
struct PostProcessSettings {
    bool bloom = true;
    AntiAliasing aa = AntiAliasing::Fxaa;
    bool motionBlur = false;
    bool volumetricFog = false;
};

// 開発側が決める調整値(assets/config/render_tuning.json の bloomThreshold・bloomStrength・bloomScatter)。
struct PostProcessTuning {
    float bloomStrength = 0.5f;
    float bloomThreshold = 0.5f;
    float bloomScatter = 0.7f;
};

// TAA を使うか。ジッタ・深度プリパス・速度パス・解決パスの条件はすべてこの1つに従う。
constexpr bool TemporalAaActive(const PostProcessSettings& settings) { return settings.aa == AntiAliasing::Taa; }

// モーションブラーを使うか。深度プリパス・速度パス・ぼかしのパスの条件はこの1つに従う
// (パスは FrameRenderer が持つ。TAA と同じく、チェーンの外で走る)。
constexpr bool MotionBlurActive(const PostProcessSettings& settings) { return settings.motionBlur; }

// フォトモードの被写界深度を掛けるか。カメラ画面のファインダーが出ているときだけ走る(フォトモード専用なので、
// ユーザー設定のスイッチは無い)。パスは FrameRenderer が持ち、TAA・モーションブラーと同じくチェーンの外で走る。
constexpr bool DepthOfFieldActive(bool finderVisible) { return finderVisible; }

// 撮影カメラの、被写界深度に使う値(焦点距離[mm]・絞り(F値)・フォーカス距離[m])。
struct DepthOfFieldCamera {
    double focalLengthMm = 35.0;
    double aperture = 5.6;
    double focusDistanceM = 10.0;
};

// そのフレームでポストプロセスのチェーンが走らせる段。TAA・モーションブラー・被写界深度はチェーンの外で走り、
// 条件は TemporalAaActive / MotionBlurActive / DepthOfFieldActive が決めるので、ここには含めない。
struct PostProcessPlan {
    bool bloom = false;
    bool fxaa = false;
};

// bloom は、設定がオンで、強さが0より大きく、トーンマップが素通し(TonemapMode::Off)でないときだけ走る。
// 素通しは、プラグインの空が有効なときと、パススルー指定(--setting tonemap=off・スモークテスト)のとき。
// RailSim II の見た目に合わせる経路に、新しい滲みを足さないための条件。
// fxaa は、設定が FXAA なら素通しのときも走る(輪郭のギザギザは空の種類に関係なく出るため)。
PostProcessPlan PlanStages(const PostProcessSettings& settings, const PostProcessTuning& tuning,
                           const TonemapParams& tonemapParams);

} // namespace rs::render
