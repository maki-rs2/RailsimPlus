#pragma once

namespace rs::render {

struct ParticleRenderTuning {
    // 画面平面の速度に掛け、板の長辺へ足す時間[s]。
    double stretchSeconds = 0.05;
    // 板の長さを幅で割った比の上限。
    double maxStretchRatio = 6.0;
    // 不透明面との深度差がこの距離[m]に達するまで透明度を下げる。
    double softDepthMeters = 0.5;
    // 視線方向の深さ[m]。start以下で透明、end以上で元の透明度にする。
    double nearFadeStartMeters = 0.3;
    double nearFadeEndMeters = 1.5;
    double ambientScale = 1.0;
    double sunScale = 1.0;

    static const ParticleRenderTuning& Embedded();
};

} // namespace rs::render
