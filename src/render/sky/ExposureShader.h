//
// ExposureShader.h - 暗順応の倍率と、IBLの明るさの補正の HLSL 関数
//
// gAdaptation(x=暗順応の結果(1x1)のSRV絶対番号 y=標準の空の暗順応が有効か)を持つ cbuffer の後ろへ連結する。
// メッシュのシェーダとボリュメトリックフォグが同じ式を使う。
//
#pragma once

namespace rs::render {

inline constexpr const char* kExposureShader = R"HLSL(
// 暗順応の倍率(プリ露出。IblGenerator が1x1へ描く)。標準の空の暗順応(gAdaptation.y)が有効なときだけ。IBLの生成前(中身が0)は1。
// 直射光(太陽・月)に掛ける。IBLはキューブ側(環境ソースの焼き込み)で掛け済みなので、ここでは掛けない。発光にも掛けない。
float AdaptationGainFromTexture() {
    if (gAdaptation.y == 0u) return 1.0;
    Texture2D<float4> exposure = gBindlessTex2D[gAdaptation.x];
    return max(exposure.Load(int3(0, 0, 0)).x, 1.0);
}

// IBLのキューブは太陽が閾値だけ動くまで作り直さない。キューブを作った時点と今とで、空の明るさ(E)と暗順応の倍率が
// どれだけ変わったかの比(画素0=今、画素1=焼き込み時の E×倍率)で、IBL(拡散・鏡面)の明るさを補正する。
// 補正しないと、薄明で作り直すたびに環境光の明るさが段になる。焼き込み前(画素1が0)・暗順応が無効なときは1。
static const float kIblExposureCorrectionMin = 0.125;
static const float kIblExposureCorrectionMax = 8.0;
float IblExposureCorrection() {
    if (gAdaptation.y == 0u) return 1.0;
    Texture2D<float4> exposure = gBindlessTex2D[gAdaptation.x];
    float4 now = exposure.Load(int3(0, 0, 0));
    float4 baked = exposure.Load(int3(1, 0, 0));
    float bakedProduct = baked.x * baked.y;
    if (bakedProduct <= 0.0) return 1.0;
    return clamp(now.x * now.y / bakedProduct, kIblExposureCorrectionMin, kIblExposureCorrectionMax);
}
)HLSL";

} // namespace rs::render
