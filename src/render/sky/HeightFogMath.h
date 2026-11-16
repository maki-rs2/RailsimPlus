//
// HeightFogMath.h - 高さフォグの光学的厚さのCPU側の基準実装(純関数)
//
// 密度は ρ(h) = ρ0 × exp(-(h - h0) / H)。視線に沿った積分を式で解く。
// メッシュのシェーダ(MeshFogShader.h の HeightFogOpticalDepth)と同じ式。変えるときは両方(テストはこちらを検証する)。
//
#pragma once

namespace rs::render::height_fog {

// 指数の引数をこの範囲へ収める(exp のオーバーフロー・アンダーフロー防止)。HLSL の kHeightFogExponentLimit と同じ値。
inline constexpr double kExponentLimit = 30.0;

// カメラと画素の高さの差が H に対してこれより小さいときは、積分の式の分母が0に近づくので、中点の高さの密度 × 距離で近似する。
// 近似の誤差は差の2乗に比例し(1e-3 なら 1e-7 より小さい)、式のままだと float の桁落ちが目立つため。
// HLSL の kHeightFogSmallSlope と同じ値。
inline constexpr double kSmallSlope = 1.0e-3;

// カメラ(高さ cameraHeight)から画素(高さ pixelHeight)までの視線に沿った光学的厚さ τ(0以上)。
//   density=ρ0 [1/m]、heightScale=H [m]、baseHeight=h0 [m]、distance=視線の長さ [m]。高さはワールドの Y。
double OpticalDepth(double density, double heightScale, double baseHeight, double cameraHeight, double pixelHeight,
                    double distance);

// 太陽の仰角の正弦(sinElevation)がこれより小さいときは、この値として扱う。地平線に近い太陽は視線が霧の中を
// 無限に長く進むので、割り算が発散しないよう下限を置く(0.01 は仰角約0.6度で、透過率はほぼ0になる)。
inline constexpr double kMinSunSine = 0.01;

// カメラ(高さ cameraHeight)から太陽の向き(仰角の正弦 sinElevation)へ無限遠まで進む視線の光学的厚さ τ(0以上)。
// 密度が高さだけで決まるので、積分は閉じた式 ρ0 × H × exp(-(cameraHeight - h0) / H) / sinElevation になる。
// レンズフレアの強さに掛ける透過率に使う。引数の単位は OpticalDepth と同じ。
double SunOpticalDepth(double density, double heightScale, double baseHeight, double cameraHeight,
                       double sinElevation);

// 透過率 exp(-τ)。
double Transmittance(double opticalDepth);

} // namespace rs::render::height_fog
