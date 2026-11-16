#pragma once

#include <format>
#include <string>

namespace rs::render {

// ACES(Narkowicz近似)。
inline constexpr float kAcesA = 2.51f;
inline constexpr float kAcesB = 0.03f;
inline constexpr float kAcesC = 2.43f;
inline constexpr float kAcesD = 0.59f;
inline constexpr float kAcesE = 0.14f;

// ACES の Hill 版フィットは、リニアsRGBをAP1へ変換してRRT+ODTのカーブを掛ける。
// 入力を kAcesFittedExposureScale で割る露出補正が、このフィットの前提。
// 行列の各行の和は1なので、灰色は灰色のまま。
inline constexpr float kAcesFittedExposureScale = 0.6f;
inline constexpr float kAcesFittedInput[3][3] = {
    {0.59719f, 0.35458f, 0.04823f},
    {0.07600f, 0.90834f, 0.01566f},
    {0.02840f, 0.13383f, 0.83777f}};
inline constexpr float kAcesFittedOutput[3][3] = {
    {1.60475f, -0.53108f, -0.07367f},
    {-0.10208f, 1.10813f, -0.00605f},
    {-0.00327f, -0.07276f, 1.07602f}};
inline constexpr float kAcesFittedA0 = 0.0245786f;
inline constexpr float kAcesFittedA1 = 0.000090537f;
inline constexpr float kAcesFittedB0 = 0.983729f;
inline constexpr float kAcesFittedB1 = 0.4329510f;
inline constexpr float kAcesFittedB2 = 0.238081f;

// Hable の公開式と、RailSim II -k-build が白色点の補正に使う倍率。
inline constexpr float kHableA = 0.15f;
inline constexpr float kHableB = 0.50f;
inline constexpr float kHableC = 0.10f;
inline constexpr float kHableD = 0.20f;
inline constexpr float kHableE = 0.02f;
inline constexpr float kHableF = 0.30f;
inline constexpr float kHableScale = 2.758f;

// Khronos PBR Neutral の公開式。暗部のオフセットと明部の圧縮・脱色を決める。
inline constexpr float kNeutralStartCompression = 0.8f - 0.04f;
inline constexpr float kNeutralToeEnd = 0.08f;
inline constexpr float kNeutralOffset = 0.04f;
inline constexpr float kNeutralDesaturation = 0.15f;

// AgX の最小実装の公開式。入力・出力の行列は行ベクトルではなく行で持つ。
inline constexpr float kAgxInset[3][3] = {
    {0.842479062253094f, 0.0784335999999992f, 0.0792237451477643f},
    {0.0423282422610123f, 0.878468636469772f, 0.0791661274605434f},
    {0.0423756549057051f, 0.0784336f, 0.879142973793104f}};
inline constexpr float kAgxOutset[3][3] = {
    {1.19687900512017f, -0.0980208811401368f, -0.0990297440797205f},
    {-0.0528968517574562f, 1.15190312990417f, -0.0989611768448433f},
    {-0.0529716355144438f, -0.0980434501171241f, 1.15107367264116f}};
inline constexpr float kAgxMinEv = -12.47393f;
inline constexpr float kAgxMaxEv = 4.026069f;
inline constexpr float kAgxDisplayGamma = 2.2f;

// triple section の公開式。暗部・直線・明部の3区間の接続と黒の持ち上げを決める。
inline constexpr float kTripleSectionMaxBrightness = 1.0f;
inline constexpr float kTripleSectionContrast = 1.0f;
inline constexpr float kTripleSectionLinearStart = 0.22f;
inline constexpr float kTripleSectionLinearLength = 0.4f;
inline constexpr float kTripleSectionBlackTightness = 1.33f;
inline constexpr float kTripleSectionPedestal = 0.0f;

// AgX の対数域を表示域へ写す6次近似多項式(高次から定数項の順)。
inline constexpr float kAgxPolynomial[7] = {15.5f,   -40.14f, 31.96f,   -6.868f,
                                            0.4298f, 0.1191f, -0.00232f};

// float の有効桁を保って HLSL へ展開し、CPU
// と同じ係数をシェーダキャッシュのキーにも含める。
inline std::string BuildTonemapCoefficientsShader() {
  std::string source;
  source += std::format("static const float kAcesA = {:.9g};\n", kAcesA);
  source += std::format("static const float kAcesB = {:.9g};\n", kAcesB);
  source += std::format("static const float kAcesC = {:.9g};\n", kAcesC);
  source += std::format("static const float kAcesD = {:.9g};\n", kAcesD);
  source += std::format("static const float kAcesE = {:.9g};\n", kAcesE);
  source +=
      std::format("static const float kAcesFittedExposureScale = {:.9g};\n",
                  kAcesFittedExposureScale);
  source += std::format(
      "static const float3x3 kAcesFittedInput = float3x3("
      "{:.9g}, {:.9g}, {:.9g}, "
      "{:.9g}, {:.9g}, {:.9g}, "
      "{:.9g}, {:.9g}, {:.9g});\n",
      kAcesFittedInput[0][0], kAcesFittedInput[0][1], kAcesFittedInput[0][2],
      kAcesFittedInput[1][0], kAcesFittedInput[1][1], kAcesFittedInput[1][2],
      kAcesFittedInput[2][0], kAcesFittedInput[2][1], kAcesFittedInput[2][2]);
  source += std::format("static const float3x3 kAcesFittedOutput = float3x3("
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g});\n",
                        kAcesFittedOutput[0][0], kAcesFittedOutput[0][1],
                        kAcesFittedOutput[0][2], kAcesFittedOutput[1][0],
                        kAcesFittedOutput[1][1], kAcesFittedOutput[1][2],
                        kAcesFittedOutput[2][0], kAcesFittedOutput[2][1],
                        kAcesFittedOutput[2][2]);
  source += std::format("static const float kAcesFittedA0 = {:.9g};\n",
                        kAcesFittedA0);
  source += std::format("static const float kAcesFittedA1 = {:.9g};\n",
                        kAcesFittedA1);
  source += std::format("static const float kAcesFittedB0 = {:.9g};\n",
                        kAcesFittedB0);
  source += std::format("static const float kAcesFittedB1 = {:.9g};\n",
                        kAcesFittedB1);
  source += std::format("static const float kAcesFittedB2 = {:.9g};\n",
                        kAcesFittedB2);
  source += std::format("static const float kHableA = {:.9g};\n", kHableA);
  source += std::format("static const float kHableB = {:.9g};\n", kHableB);
  source += std::format("static const float kHableC = {:.9g};\n", kHableC);
  source += std::format("static const float kHableD = {:.9g};\n", kHableD);
  source += std::format("static const float kHableE = {:.9g};\n", kHableE);
  source += std::format("static const float kHableF = {:.9g};\n", kHableF);
  source +=
      std::format("static const float kHableScale = {:.9g};\n", kHableScale);
  source +=
      std::format("static const float kNeutralStartCompression = {:.9g};\n",
                  kNeutralStartCompression);
  source += std::format("static const float kNeutralToeEnd = {:.9g};\n",
                        kNeutralToeEnd);
  source += std::format("static const float kNeutralOffset = {:.9g};\n",
                        kNeutralOffset);
  source += std::format("static const float kNeutralDesaturation = {:.9g};\n",
                        kNeutralDesaturation);
  source += std::format("static const float3x3 kAgxInset = float3x3("
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g});\n",
                        kAgxInset[0][0], kAgxInset[0][1], kAgxInset[0][2],
                        kAgxInset[1][0], kAgxInset[1][1], kAgxInset[1][2],
                        kAgxInset[2][0], kAgxInset[2][1], kAgxInset[2][2]);
  source += std::format("static const float3x3 kAgxOutset = float3x3("
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g}, "
                        "{:.9g}, {:.9g}, {:.9g});\n",
                        kAgxOutset[0][0], kAgxOutset[0][1], kAgxOutset[0][2],
                        kAgxOutset[1][0], kAgxOutset[1][1], kAgxOutset[1][2],
                        kAgxOutset[2][0], kAgxOutset[2][1], kAgxOutset[2][2]);
  source += std::format("static const float kAgxMinEv = {:.9g};\n", kAgxMinEv);
  source += std::format("static const float kAgxMaxEv = {:.9g};\n", kAgxMaxEv);
  source += std::format("static const float kAgxDisplayGamma = {:.9g};\n",
                        kAgxDisplayGamma);
  source +=
      std::format("static const float kTripleSectionMaxBrightness = {:.9g};\n",
                  kTripleSectionMaxBrightness);
  source += std::format("static const float kTripleSectionContrast = {:.9g};\n",
                        kTripleSectionContrast);
  source +=
      std::format("static const float kTripleSectionLinearStart = {:.9g};\n",
                  kTripleSectionLinearStart);
  source +=
      std::format("static const float kTripleSectionLinearLength = {:.9g};\n",
                  kTripleSectionLinearLength);
  source +=
      std::format("static const float kTripleSectionBlackTightness = {:.9g};\n",
                  kTripleSectionBlackTightness);
  source += std::format("static const float kTripleSectionPedestal = {:.9g};\n",
                        kTripleSectionPedestal);
  source += std::format("static const float kAgxPolynomial0 = {:.9g};\n",
                        kAgxPolynomial[0]);
  source += std::format("static const float kAgxPolynomial1 = {:.9g};\n",
                        kAgxPolynomial[1]);
  source += std::format("static const float kAgxPolynomial2 = {:.9g};\n",
                        kAgxPolynomial[2]);
  source += std::format("static const float kAgxPolynomial3 = {:.9g};\n",
                        kAgxPolynomial[3]);
  source += std::format("static const float kAgxPolynomial4 = {:.9g};\n",
                        kAgxPolynomial[4]);
  source += std::format("static const float kAgxPolynomial5 = {:.9g};\n",
                        kAgxPolynomial[5]);
  source += std::format("static const float kAgxPolynomial6 = {:.9g};\n",
                        kAgxPolynomial[6]);
  return source;
}

} // namespace rs::render
