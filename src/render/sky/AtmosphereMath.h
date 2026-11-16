//
// AtmosphereMath.h - 大気LUTのCPU側の基準実装(純関数)
//
// Sky-View LUTのUV割り当てはシェーダ(AtmosphereShader.h)の SkyViewParamsToUv /
// UvToSkyViewParams と同じ式。変えるときは両方(テストはこちらを検証する)。
// 観測者の高さは引数で受け取り、本編では atmosphere::kBottomRadiusKm + kObserverHeightKm を渡す。
//
#pragma once

#include <array>

#include "../../core/Math.h"

namespace rs::render::atmosphere {

// Sky-View LUTを引くための視線の2つの角度。
struct SkyViewDirection {
    double viewZenithCos = 1.0; // 視線と天頂のなす角のcos
    double lightViewCos = 1.0;  // 視線と太陽の、水平面へ射影した方位差のcos
};

// 太陽の高度(観測者から見た向きのy成分)が前回生成時から閾値以上変わったか。方位の変化は数えない。
// 前回の値が無い(hasPrevious=false)ときは常にtrue。
bool ShouldRegenerateSkyView(bool hasPrevious, const rs::core::Vec3& previousSun,
                             const rs::core::Vec3& currentSun, double thresholdDeg);

// カメラのワールド高さ[m]から、大気の観測者の高さ[km、地表から]を求める。ワールド高さ0が基準の高さ(kObserverHeightKm)で、
// 上がるぶんだけ足す。0より下は基準の高さ、上限は kObserverHeightMaxKm。
double ObserverHeightKmFromCameraMeters(double cameraHeightMeters);

// 観測者の高さが前回のSky-View LUTの生成時から thresholdKm 以上変わったか。
bool ShouldRegenerateForObserverHeight(double previousKm, double currentKm, double thresholdKm);

// 方向(正規化していなくてよい。+Yが天頂)の高度[度]。長さ0なら0。
double DirectionElevationDeg(const rs::core::Vec3& direction);

// 月の光(IBLの環境ソースが含む月明かりの空)が前回のIBL生成時から変わって、IBLを作り直すか。
// 前回・今回とも比が0なら false。0との行き来は true。比が相対で kMoonIblRegenerateRatioChange 以上変わった、
// または方向のなす角(高度と方位の両方)が thresholdDeg 以上のとき true。
bool ShouldRegenerateIblForMoon(const rs::core::Vec3& previousMoon, float previousRatio,
                                const rs::core::Vec3& currentMoon, float currentRatio, double thresholdDeg);

// 観測者の高さ(惑星中心から、km)での地平線の方向のcos(sqrt(h^2-R^2)/h)。
double SkyViewHorizonCos(double viewHeightKm);

// 視線が地面に当たるか(地平線より下を向いているか)。
bool SkyViewIntersectsGround(double viewZenithCos, double viewHeightKm);

// UV(0..1、サブテクセル補正込み)から視線の角度を求める。
SkyViewDirection SkyViewDirectionFromUv(double u, double v, double viewHeightKm);

// 視線の角度からUVを求める(SkyViewDirectionFromUvの逆)。
void SkyViewUvFromDirection(const SkyViewDirection& direction, double viewHeightKm, double& u,
                            double& v);

// ---- IBLのキューブマップ(IblShader.h と同じ式。変えるときは両方) ----

// キューブの面の画素が表す方向(正規化済み、Y-up)。
struct CubeDirection {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// 面 face(0〜5 = +X,-X,+Y,-Y,+Z,-Z。D3D標準の並び)の、画素の位置 (tx,ty)∈[0,1](tyは下向き)の方向。
// u=2tx-1、v=2ty-1 として +X=(1,-v,-u)、-X=(-1,-v,u)、+Y=(u,1,v)、-Y=(u,-1,-v)、
// +Z=(u,-v,1)、-Z=(-u,-v,-1) を正規化したもの。
CubeDirection CubeFaceDirection(int face, double tx, double ty);

// 鏡面キューブのミップ番号 mip(0〜mipCount-1)の roughness。mip/(mipCount-1) の線形。
double IblSpecularMipRoughness(int mip, int mipCount);

// ---- 暗順応(IblShader.h の AdaptationGain と同じ式。変えるときは両方) ----

// 順応の照度 adaptationLuminance(太陽の放射照度πに対する比の輝度。IblGenerator が測る E)から、
// トーンマップの露出へ掛ける倍率を求める。E が start 以上なら1、そうでなければ clamp((start / E)^exponent, 1, maxGain)。
// 暗いほど大きく単調に増え、maxGain で止まる。exponent が0なら常に1。
double NightAdaptationGain(double adaptationLuminance, double start, double exponent, double maxGain);

// 順応の倍率。夜の曲線(nightStart・nightExponent)と昼の曲線(dayStart・dayExponent。どちらも
// NightAdaptationGain と同じ形)の大きいほうを返す(IblShader.h の ExposureAdaptationGain と同じ合成。変えるときは両方)。
// どちらの曲線が勝つかは調整値で決まる(開始の照度が高い昼の曲線は、2本が交わる照度より明るい範囲で勝つ)。
// dayExponent が0なら夜の曲線だけと同じ。
double ExposureAdaptationGain(double adaptationLuminance, double nightStart, double nightExponent, double dayStart,
                              double dayExponent, double maxGain);

// 深夜の暗順応の上乗せ(NightAdaptationGain の結果に掛ける係数)。太陽高度 sunElevationDeg[度]が
// kNightBoostStartSunElevationDeg 以上なら1、kNightBoostFullSunElevationDeg 以下なら boost、その間は
// 対数の上で直線につなぐ(boost^t)。boost が1未満なら1として扱う。月の有無によらず深夜の画面全体を一律に明るくする。
double NightAdaptationBoost(double sunElevationDeg, double boost);


// ---- 大気遠近(aerial perspective。AP)のLUT(AerialPerspectiveShader.h と同じ式。変えるときは両方) ----

// APのLUTは、距離方向のスライスを横に並べた1枚の2Dテクスチャ(アトラス)。幅 = スライス数 × タイルの大きさ、
// 高さ = タイルの大きさ。各スライス(タイル)は画面方向の 縦横 tileSize 画素。
//
// APのLUTの奥行き方向は、距離を手前ほど細かく割る(スライス位置 unit∈[0,1] と距離 = 最大距離 × unit^2)。
// unit=0 が観測者の位置(距離0)、unit=1 が最大距離。
double AerialDistanceKmFromUnit(double unit, double maxDistanceKm);

// 距離からスライス位置 unit∈[0,1] を求める(AerialDistanceKmFromUnit の逆)。最大距離より遠い距離は1で止める。
double AerialUnitFromDistanceKm(double distanceKm, double maxDistanceKm);

// 奥行きのテクスチャ座標 w∈[0,1](端のテクセル中心へ収める補正込み)と、スライス位置 unit の変換。
double AerialDepthWFromUnit(double unit, int sliceCount);
double AerialUnitFromDepthW(double w, int sliceCount);

// テクセルの添字 index(0〜count-1)が表す位置 unit∈[0,1]。端のテクセル(0と count-1)が unit=0 と 1 に当たる
// (タイルの縁を、隣のスライスへにじませずに画面の端の視線へ合わせるため)。スライス位置にも画面方向にも使う。
double AerialUnitFromTexelIndex(int index, int count);

// アトラスのテクスチャ座標 u(0〜1)。スライス slice のタイルの中の位置 unitX∈[0,1]
// (端のテクセル中心へ収める補正込み)を指す。引く側は、隣り合う2スライスのこの座標を引いて手で補間する。
double AerialAtlasU(double unitX, int slice, int tileSize, int sliceCount);

// カメラの向きと視野から、APのLUTの視線方向を作る定数(HLSLの定数バッファと同じ並び、12 DWORD)。
// right・up・forward はワールドの単位ベクトル(左手系・Y-up)、tanHalfFovX/Y は視野の半分の正接。
struct AerialCameraFrame {
    float right[3] = {1.0f, 0.0f, 0.0f};
    float tanHalfFovX = 1.0f;
    float up[3] = {0.0f, 1.0f, 0.0f};
    float tanHalfFovY = 1.0f;
    float forward[3] = {0.0f, 0.0f, 1.0f};
    float padding = 0.0f;
};

// ビュー行列(row-vector、LookAt由来。列がカメラの右・上・前)と縦の視野角・縦横比から作る。
// メインの描画(射影 PerspectiveLH)の画素の視線と一致する。
AerialCameraFrame MakeAerialCameraFrame(const rs::core::Mat4& view, float fovYRadians, float aspectRatio);

// 画面上の位置 (unitX, unitY)∈[0,1]² の視線方向(正規化済み。unitX は左から右、unitY は上から下)。
// 0と1は画面の端の視線。forward + (2x-1)·tanHalfFovX·right − (2y-1)·tanHalfFovY·up を正規化する。
std::array<double, 3> AerialViewDirection(const AerialCameraFrame& frame, double unitX, double unitY);

// 観測者(惑星中心からの距離 observerRadiusKm)から、天頂とのなす角のcosが viewZenithCos の向きへ
// distanceKm だけ進む区間の透過率(RGB)。大気の消散(レイリー・ミー・オゾン)を区間の分割数 steps の
// 中点法で積分して exp(-光学的厚さ) を返す。aerosolOpticalDepth はミーの濃さ(render_tuning.json のAOD)。
std::array<double, 3> AerialTransmittance(double observerRadiusKm, double viewZenithCos, double distanceKm,
                                          double aerosolOpticalDepth, int steps = 64);

} // namespace rs::render::atmosphere
