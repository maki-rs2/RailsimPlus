#pragma once

namespace rs::core {

// 長さ[m]・半径[m]・区間の長さをゼロとみなす下限。これ以下は「長さが無い」「直線(半径無限)」として扱い、
// 割り算の分母にしない。
inline constexpr double kZeroLengthEps = 1e-9;

// 方向ベクトル・接線・横方向ベクトルの長さがこれ未満なら、向きを決められない(縮退)とみなして
// 代わりの向きを使う。正規化の前の判定に使う。
inline constexpr double kDegenerateVectorEps = 1e-6;

// 同次座標のwがこれ未満なら、透視除算できない(無限遠)とみなす。
inline constexpr double kHomogeneousWEps = 1e-8;

// ベクトル・平面の長さがこれ以下なら、正規化せずゼロ扱いにする。
inline constexpr double kNormalizeLengthEps = 1e-8;

// レイの方向成分や交差計算の分母がこれ以下なら、平行とみなして交点なしにする。
inline constexpr double kRayParallelEps = 1e-8;

// 曲線半径[m]がこれ未満なら直線として扱う(カント・押し出しの曲げを掛けない)。
inline constexpr double kStraightRadiusEpsM = 1e-3;

// 部品を繰り返し並べる間隔[m]がこれ以下なら、間隔が無効とみなして1個だけ置く(割り算の発散を避ける)。
inline constexpr double kMinRepeatIntervalM = 1e-3;

// 軌道の弧長[m]を補間・進退するときに同一点とみなす幅。
inline constexpr float kTrackPositionEpsM = 1e-6f;
// 幾何ソルバで短い弦・水平弧長[m]を縮退として扱う幅。
inline constexpr float kGeometryLengthEpsM = 1e-4f;
// 点列上の射影で線分長の二乗[m²]をゼロとみなす幅。用途ごとの既存精度を保つ。
inline constexpr double kTrackSegmentLengthSquaredEpsM2 = 1e-12;
inline constexpr float kPlacementSegmentLengthSquaredEpsM2 = 1e-6f;
inline constexpr float kHorizontalSegmentLengthSquaredEpsM2 = 1e-8f;
// 画面上の射影で線分長の二乗[px²]をゼロとみなす幅。
inline constexpr float kScreenSegmentLengthSquaredEpsPx2 = 1e-6f;
// レイの方向ベクトルの長さの二乗(無次元)をゼロとみなす幅。
inline constexpr float kRayDirectionLengthSquaredEps = 1e-8f;
// 接線・角度の計算で、ほぼ平行な方向を同一とみなす幅(無次元)。
inline constexpr float kDirectionParallelEps = 1e-6f;
// 水平接線の長さ(無次元)がこれ未満なら鉛直とみなす。
inline constexpr float kHorizontalDirectionEps = 1e-4f;
// 接線の角度差[rad]がこれ未満なら直線として扱う。
inline constexpr float kLineAngleEpsRad = 1e-6f;
// biarcの半旋回角[rad]を直線とみなす幅。
inline constexpr float kBiarcHalfAngleEpsRad = 1e-4f;
// 曲率[1/m]の連続性を判定する幅。
inline constexpr float kCurvatureContinuityEpsPerM = 1e-4f;
// 行列の逆行列計算で、ピボットをゼロとみなす幅(無次元)。
inline constexpr double kMatrixPivotEps = 1e-12;
// 剛体変換の回転要素(無次元)・並進[m]を恒等変換とみなす幅。
inline constexpr double kRigidRotationIdentityEps = 1e-12;
inline constexpr double kRigidTranslationIdentityEpsM = 1e-12;
// 倍精度の方向計算で、除算の発散を避ける下限(無次元)。
inline constexpr double kDirectionDivisionEps = 1e-12;
inline constexpr double kDirectionUnderflowEps = 1e-300;
// 水平2接線の交点計算で行列式(無次元)を平行とみなす幅。
inline constexpr float kHorizontalIntersectionDetEps = 1e-9f;
// 画面上の線分を矩形へ切り詰めるとき、成分[px]を平行とみなす幅。
inline constexpr float kScreenParallelEpsPx = 1e-9f;
// 配置の弧長[m]・横ずれ[m]を同一位置とみなす幅。
inline constexpr double kPlacementPositionEpsM = 1e-6;
inline constexpr float kPlacementLateralEpsM = 1e-4f;
// 弧長[m]の連結境界で端点の丸め誤差を吸収する幅。
inline constexpr float kChainBoundaryEpsM = 1e-4f;
// 取り込み点列への水平射影で線分長の二乗[m²]をゼロとみなす幅。
inline constexpr float kImportedSegmentLengthSquaredEpsM2 = 1e-9f;
// 取り込み点列の偏角[rad]・外積の長さを平行とみなす幅。
inline constexpr float kImportedLineAngleEpsRad = 1e-3f;
// 円弧復元で半偏角の正弦(無次元)がゼロに近いとみなす幅。
inline constexpr double kImportedHalfAngleSinEps = 1e-9;
// 敷設プレビューで半径の相対差(無次元)を同一とみなす幅。
inline constexpr float kPreviewRadiusRelativeEps = 1e-3f;
// 複線の始点列が横方向に並ぶとみなす接線の内積(無次元)の幅。
inline constexpr float kParallelTrackAlignmentEps = 1e-3f;
// 架線の端点間隔[m]がこれ未満なら線を張らない。
inline constexpr float kWireSpanLengthEpsM = 1e-3f;
// カメラの回転半径[m]・ドラッグで得る接線の長さ[m]が縮退したとみなす幅。
inline constexpr float kCameraRadiusEpsM = 1e-4f;
inline constexpr float kDragTangentLengthEpsM = 1e-3f;
// 画面の操作軸を射影するとき、線分の二乗長[px²]・投影長[px]を無効とみなす幅。
inline constexpr float kGizmoSegmentLengthSquaredEpsPx2 = 1e-5f;
inline constexpr float kGizmoProjectedLengthEpsPx = 1e-3f;
// 操作軸のワールド長[m]を無効とみなす幅。
inline constexpr float kGizmoWorldLengthEpsM = 1e-5f;
// テクスチャの1コマ幅(UV比)・1コマの時間[s]・周期[s]・更新周波数[Hz]を無効とみなす幅。
inline constexpr float kAnimationUvFrameWidthEps = 1e-6f;
inline constexpr float kAnimationFrameDurationEpsS = 1e-9f;
inline constexpr float kAnimationCycleEpsS = 1e-6f;
inline constexpr float kAnimationFrameRateEpsHz = 1e-3f;
// カントの比率(無次元)がこれ以下なら曲げを掛けない。
inline constexpr float kCantRatioEps = 1e-6f;
// 制動の二次項の係数[km/h/s²]をゼロとみなす幅。
inline constexpr double kBrakingQuadraticCoefficientEps = 1e-12;
// 時間を刻みに分ける回数(無次元)の切り上げで整数境界の誤差を吸収する幅。
inline constexpr double kSubstepCountEps = 1e-9;
// 指数飽和の上限をゼロとみなす幅(入力と同じ単位)。
inline constexpr double kSoftClampScaleEps = 1e-9;
// 車輪の回転量を計算するときに分母の距離[m]をゼロとみなす幅。
inline constexpr double kAxleRotationDistanceEpsM = 1e-12;
// 影のフェード幅[deg]と、影の1テクセルのワールド長[m]の除算下限。
inline constexpr double kShadowFadeAngleEpsDeg = 1e-3;
inline constexpr float kShadowTexelSizeEpsM = 1e-4f;

} // namespace rs::core
