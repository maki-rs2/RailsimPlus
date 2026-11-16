//
// AerialPerspectiveDebug.h - 大気遠近(AP)のLUTの中身を読み戻してログへ出す開発用の確認
//
// 環境変数 RS_DEBUG_APLUT=1 のときだけ働く(起動後の最初の呼び出しで1回だけ読む)。LUTを描いた直後の
// 数フレーム目にLUTを読み戻し用バッファへ写し、さらに数フレーム後に取り出して、手前/奥 × 水平/上向きの画素の
// rgb と a(透過率のRGB平均)を出す。水平・最大距離の a は、CPUの基準実装(AerialTransmittance)とも比べる。
// ゲームの描画経路は読み戻しに依存しない。
//
#pragma once

#include <memory>

#include "../../core/Math.h"
#include "../postprocess/SceneHdrTarget.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "AtmosphereMath.h"
#include "AtmosphereParams.h"

namespace rs::render {

// 読み戻したLUTの画素を解釈するための、LUTの大きさと濃さ。
struct AerialDebugLayout {
    int sliceCount = 0;
    int tileSize = 0;
    float maxDistanceKm = 0.0f;
    float aerosolOpticalDepth = 0.0f;
    float observerHeightKm = atmosphere::kObserverHeightKm;
};

class AerialPerspectiveDebug {
public:
    AerialPerspectiveDebug();
    ~AerialPerspectiveDebug();
    AerialPerspectiveDebug(const AerialPerspectiveDebug&) = delete;
    AerialPerspectiveDebug& operator=(const AerialPerspectiveDebug&) = delete;

    static bool Enabled();

    // LUTを描いたフレームごとに、LUTを描いた直後に呼ぶ。LUTはSRV状態であること。無効のときは何もしない。
    // camera・sunDirectionToSun は、このフレームのLUTを描いたときの値。
    void Step(const rs::rhi::Device& device, const rs::rhi::CommandList& commandList, SceneHdrTarget& lut,
              const AerialDebugLayout& layout, const atmosphere::AerialCameraFrame& camera,
              const rs::core::Vec3& sunDirectionToSun);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
