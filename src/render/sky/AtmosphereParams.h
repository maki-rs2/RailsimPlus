//
// AtmosphereParams.h - 大気LUT(Hillaire 2020)の物理パラメータとLUTの大きさ
//
// 地球の標準値で、調整値ではない(JSONには出さない)。エアロゾルの量(AOD)だけは天候で変わるので
// render_tuning.json の atmosphereAerosolOpticalDepth から受け取る(MieScatteringPerKm ほか)。シェーダ(AtmosphereShader.h)の定数と
// CPU側の基準実装(AtmosphereMath)の両方がこの値を使う。距離の単位はkm。
//
#pragma once

#include <numbers>

namespace rs::render::atmosphere {

// 惑星と大気の範囲。
constexpr float kBottomRadiusKm = 6360.0f;
constexpr float kTopRadiusKm = 6460.0f;

// レイリー散乱(RGB、/km)とスケールハイト。
constexpr float kRayleighScatteringPerKm[3] = {0.005802f, 0.013558f, 0.0331f};
constexpr float kRayleighScaleHeightKm = 8.0f;

// ミー(エアロゾル)のスケールハイト、位相関数の非対称パラメータ、単一散乱アルベド(消散のうち散乱の割合)。
constexpr float kMieScaleHeightKm = 1.2f;
constexpr float kMiePhaseG = 0.8f;
constexpr float kMieSingleScatteringAlbedo = 0.9f;

// 地上のミーの消散係数(/km)。エアロゾルの光学的厚さ(AOD。鉛直に積分した消散)は、密度 exp(-h/H) の
// 積分が H なので、消散係数 × スケールハイト。AOD は天候で変わるので render_tuning.json の調整値から渡す。
constexpr float MieExtinctionPerKm(float aerosolOpticalDepth) {
    return aerosolOpticalDepth / kMieScaleHeightKm;
}
constexpr float MieScatteringPerKm(float aerosolOpticalDepth) {
    return MieExtinctionPerKm(aerosolOpticalDepth) * kMieSingleScatteringAlbedo;
}
constexpr float MieAbsorptionPerKm(float aerosolOpticalDepth) {
    return MieExtinctionPerKm(aerosolOpticalDepth) * (1.0f - kMieSingleScatteringAlbedo);
}

// オゾン吸収(RGB、/km)。高度kOzoneCenterHeightKmを中心に、半幅kOzoneHalfWidthKmの三角分布。
constexpr float kOzoneAbsorptionPerKm[3] = {0.000650f, 0.001881f, 0.000085f};
constexpr float kOzoneCenterHeightKm = 25.0f;
constexpr float kOzoneHalfWidthKm = 15.0f;

// 多重散乱と地面からの反射に使う地面アルベド。
constexpr float kGroundAlbedo = 0.3f;

// 観測者の基準の高さ。ワールドの高さ0のカメラはこの高さにいるものとして扱い、カメラが上がるぶんだけ足す
// (ObserverHeightKmFromCameraMeters)。Sky-View LUTのUV割り当て(地平線の角)・太陽の色(透過率の高さ)・
// サムネイルの大気・高さへの追従を切ったときは、この高さに固定する。
constexpr float kObserverHeightKm = 0.1f;

// 追従する観測者の高さの上限。大気の上端(100km)の手前で止める。Sky-View LUTのUV割り当ては基準の高さのままなので、
// 高く上がるほど地平線の角がずれる。電車のカメラが届く高さに収める。
constexpr float kObserverHeightMaxKm = 20.0f;

// 観測者の高さが前回のSky-View LUTの生成時からこの高さ[km]以上変わったら作り直す(10m)。
constexpr double kObserverHeightRegenerateKm = 0.01;

// 太陽の放射照度。現行の直射光(albedo * ndl、太陽=1.0)と単位をそろえるためπを基準にする。
constexpr float kSunIrradiance = std::numbers::pi_v<float>;

// 満月の放射照度の、太陽に対する比。満月の地上照度は約0.25ルクス、太陽は約10万ルクスなので 0.25 / 100000。
// 月の光は、太陽と同じスペクトル(月の反射率はほぼ灰色)で、この比 × MoonPhaseBrightness を掛けて太陽の式へ入れる。
constexpr float kFullMoonToSunIrradianceRatio = 2.5e-6f;

// 月の角半径[rad]。平均の視直径は約31.1分角なので、角半径は 0.2591度。空パスの月の円盤の大きさ
// (SkyShader.h:MoonDiskRadiance)。円盤の明るさは、円盤全体の放射照度が kFullMoonToSunIrradianceRatio になるよう
// この大きさから求める(反射率の定数は別に持たない)。
constexpr float kMoonAngularRadiusRad = 0.2591f * std::numbers::pi_v<float> / 180.0f;

// 月のない晴れた夜空の放射輝度(星明かり・大気光)の、太陽の放射輝度に対する比。月のない晴れた夜の地上照度は
// 約0.002ルクス、太陽は約10万ルクスなので 0.002 / 100000 = 2e-8。一様な空なら照度 = π × 放射輝度で、太陽の放射照度も π なので、
// 放射輝度の比も同じ値になる。色は白。空の向き(地面に当たらない向き)へ一様に足す(AtmosphereShader.h:SkyRadianceWithMoon)。
constexpr float kNightSkyBaseRadianceRatio = 2e-8f;

// 月の光(直射光・空・IBL)を使う、太陽高度の上限[度]。これより太陽が高いときは、月の寄与が
// 太陽の数十万分の1で見えないので 0 にする。昼に月用のSky-View LUT・IBLを作り直さないための打ち切り。
constexpr double kMoonActiveMaxSunElevationDeg = 10.0;

// 深夜の暗順応の上乗せ(RenderTuning::nightAdaptationBoost)をかけ始める・かけ切る太陽高度[度]。
// 順応の倍率(夜の曲線と昼の曲線の大きいほう。指数は RenderTuning の nightAdaptationExponent・dayAdaptationExponent)は、
// 薄明の終わりの空の落ち方に追いつかず、太陽-10度より深いと画面が暗くなっていく
// ので、この3度で1から上乗せの値まで増やして深夜の暗さを保つ。
constexpr double kNightBoostStartSunElevationDeg = -10.0;
constexpr double kNightBoostFullSunElevationDeg = -13.0;

// 月の放射照度の比が前回のIBL生成時からこの割合(相対)以上変わったら、IBLを作り直す。
constexpr float kMoonIblRegenerateRatioChange = 0.1f;

// 地面に接する点が自分の影に入らないようにするための小さな持ち上げ量。
constexpr float kPlanetRadiusOffsetKm = 0.01f;

// LUTの大きさ。形式は透過率・多重散乱がR16G16B16A16_FLOAT(kAtmosphereLutFormat)、Sky-View(太陽用・月用)がR32G32B32A32_FLOAT(kSkyViewLutFormat)。いずれもAtmosphereLuts.cpp。
constexpr unsigned int kTransmittanceLutWidth = 256;
constexpr unsigned int kTransmittanceLutHeight = 64;
constexpr unsigned int kMultiScatLutSize = 128;
constexpr unsigned int kSkyViewLutWidth = 192;
constexpr unsigned int kSkyViewLutHeight = 108;

// 積分のステップ数。
constexpr int kTransmittanceSteps = 40;
constexpr int kMultiScatSteps = 20;
constexpr int kMultiScatSqrtSamples = 8; // 方向数は8*8=64
constexpr int kSkyViewMinSteps = 8;
constexpr int kSkyViewMaxSteps = 30;

// 太陽の方向が前回生成時からこの角度[度]以上変わったときだけSky-View LUTを作り直す。
constexpr double kSkyViewRegenerateThresholdDeg = 0.05;

} // namespace rs::render::atmosphere
