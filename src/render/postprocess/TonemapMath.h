//
// TonemapMath.h - トーンマップのモード・パラメータとCPU側の基準実装
//
// トーンマップのシェーダ(TonemapShader.h)と同じ式をCPUで計算する純関数。
// シェーダを変えたらここも合わせること(テストはこちらを検証し、`--tonemap-check` がGPUとの一致を確かめる)。
//
#pragma once

#include <cstddef>
#include <cstdint>

namespace rs::render {

struct RenderTuning;

enum class TonemapMode : std::uint32_t {
    Aces = 0, // 露出を掛けてACES(Narkowicz近似。各チャンネルに別々に掛ける。tonemap=aces で選ぶ)
    Off = 1,  // 露出も掛けず saturate のみ(配管確認・スモークテスト用)
    Hable = 2, // 露出を掛けてHable(Uncharted 2)×2.758。RailSim II -k-build のPBRシェーダと同じ曲線
    Reinhard = 3, // x/(1+x)。暗部はほぼそのまま
    Neutral = 4,  // Khronos PBR Neutral(色全体で計算。暗部は2乗で沈む)
    Agx = 5,      // Blender の AgX(色全体で計算。対数の範囲へ写して多項式で近似)
    TripleSection = 6,       // triple section(暗部・直線・明部の3区間をつなぐ曲線。CEDEC 2017で公開された式)。中間が直線。既定
    AcesFitted = 7,          // ACESのRRT+sRGB向けODTのフィット(Stephen Hill版)。AP1へ行列変換して色全体で計算する。tonemap=acesfitted で選ぶ
};
// 列挙の件数(環境変数の名前の表とシェーダの分岐の数)。
inline constexpr std::size_t kTonemapModeCount = 8;
// 既定の曲線。ユーザー設定の初期値・読めない名前の戻り先・サムネイル(設定によらず固定)が使う。
inline constexpr TonemapMode kDefaultTonemapMode = TonemapMode::TripleSection;

struct TonemapParams {
    TonemapMode mode = kDefaultTonemapMode;
    float exposure = 1.0f;
};

// sRGBのエンコード値(0..1)をリニアへ変換する。
float SrgbToLinear(float srgb);

// 色(リニアRGB)のトーンマップ。結果は各チャンネル0..1。
void TonemapColor(const TonemapParams& params, const float linearRgb[3], float out[3]);

// 灰色(3チャンネルとも同じ値)のトーンマップの1チャンネル分。結果は常に0..1。
float TonemapChannel(const TonemapParams& params, float linearValue);

// TonemapChannel の逆。灰色(3チャンネルとも同じ値)で、トーンマップ後に targetLinear(0..1)になる
// 入力値を二分探索で求める。チャンネルごとに独立していないモード(Neutral・Agx)では、
// 灰色で解くので他の色には近い値にしかならない。targetLinear が出力の上限以上なら探索範囲の上端を返す。
float InverseTonemapChannel(const TonemapParams& params, float targetLinear);

// 設定文字列の値からモードを決める。名前は aces/acesfitted/off/hable/reinhard/neutral/agx/triplesection
// (大文字小文字を区別しない)。nullptr・空・その他は既定(kDefaultTonemapMode)。
TonemapMode TonemapModeFromEnvValue(const char* value);

// モードの名前(--setting tonemap・settings.datで使う小文字の名前)。範囲外は既定(kDefaultTonemapMode)の名前。
const char* TonemapModeName(TonemapMode mode);

// tuning.tonemapExposure から露出を決める(負数・NaN・無限大は1.0)。MakeTonemapParams と、
// 毎フレーム調整値から露出だけを取り直す側が共有する。
float TonemapExposureFromTuning(const RenderTuning& tuning);

// 本編・サムネイル共通の設定を作る。露出は tuning.tonemapExposure、
// モードは引数から決める。
TonemapParams MakeTonemapParams(const RenderTuning& tuning, const char* settingValue);

// 上を埋め込みの調整値と既定のトーンマップから作る入口。
TonemapParams LoadTonemapParams();

} // namespace rs::render
