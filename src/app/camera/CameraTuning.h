#pragma once

namespace rs::app {

struct CameraTuning {
    float rotationRate = 25.0f;
    float zoomRate = 12.0f;
    float moveRate = 15.0f;
    float fovRate = 10.0f;
    // FlyTo(基準点を別の場所へ移す)の所要時間[秒] = clamp(最短 + 距離 / 速さ, 最短, 最長)。
    float flyToMinSeconds = 0.35f;
    float flyToMaxSeconds = 1.2f;
    float flyToMetersPerSecond = 800.0f;
    double rollRadiansPerSecond = 0.7853981633974483; // 傾きキーを押している間の回転速度[rad/s]

    // 視点操作・視野角の可動域
    float flyMouseSensitivity = 0.005f; // 右ドラッグ視点回転の感度[rad/px]
    float flyPitchLimitRadians = 1.5533f; // ピッチ可動域(≒89度)
    float viewHeightSensitivity = 0.01f; // 運転台カメラのShift+左ドラッグの高さ変化係数
    // 俯瞰カメラのドラッグ移動(中・左ドラッグと、Shift+左ドラッグの高さ調節)の係数。
    // 注目点の移動量[m] = 移動したpx × 距離 × 係数 × 倍率。
    float viewDragPerPixel = 0.001f;
    float viewDragFastMultiplier = 10.0f; // Ctrl押下中の倍率
    float viewDistMoveMinMeters = 2.0f; // WASD移動速度係数(distance基準)の下限
    float viewDashMultiplier = 3.0f; // Ctrl押下中のWASD移動速度の倍率
    float viewZoomStepIn = 1.0f / 1.15f; // ホイール1ノッチで距離を詰める倍率
    float viewZoomStepOut = 1.15f; // ホイール1ノッチで距離を伸ばす倍率
    float viewDistanceMinMeters = 0.5f; // distanceクランプ下限
    float viewDistanceMaxMeters = 50000.0f; // distanceクランプ上限
    float fovYWheelStepRadians = 0.0174533f; // Shift+ホイール1ノッチあたり1°
    float fovYMinRadians = 0.0174533f; // 1°
    float fovYMaxRadians = 3.0543262f; // 175°

    // 運転台視点(CabinPos)のWASD+上下矢印移動速度[m/s]
    // (RailSim IIのCABIN_POS_MOVEは
    // マウスdelta単位のため流用せず、本リポジトリのWASD入力に合わせて調整した値)。
    float cabinMoveMetersPerSecond = 4.0f;
    // 運転台視点(CabinDir)の右ドラッグ感度[/px]。RailSim IIのCABIN_DIR_MOVE(0.005)と
    // 同じマウスdelta単位の操作のためそのまま踏襲する。
    float cabinDirMouseSensitivity = 0.005f;

    static const CameraTuning& Embedded();
};

} // namespace rs::app
