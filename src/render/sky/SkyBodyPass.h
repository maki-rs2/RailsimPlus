//
// SkyBodyPass.h - プラグインの空の Sun.x・Moon.x を、時刻に合わせた位置へ別メッシュとして描くパス
//
// RailSim II の CEnvPlugin::Render と同じ描き方: 空(大気の背景)の直後・不透明パスより前に、
// 深度の読み書きなし・ライティングなし・加算系ブレンドで描く(テクスチャ×材質色がそのまま出る)。
// 位置はカメラ位置 + 10 × 方向(SkyBodyMath)。Sun.x は太陽方向、Moon.x は月方向に置き、
// 月は前方を太陽と反対へ向ける(満ち欠けは月のテクスチャが持つ)。
// ブレンドは RailSim II の devBLEND_ADD2(定義は RailSim II のソースに無い)の推定で、SrcAlpha×Src + One×Dest。
// 遠景(Landscape.x のドーム)は Moon の後に、同じく深度なし・ライティングなしで、アルファブレンドで描く。
// 位置はカメラ位置そのもの(回転なし)、色はテクスチャ × 材質のアンビエント × 光の倍率(SkyBodyFrame::landscapeLight)。
// 描画先は常に kSceneHdrFormat。SRVは共有ヒープの連続3枠(先頭から Sun・Moon・遠景)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../../core/Math.h"
#include "../rhi/CommandList.h"
#include "../rhi/CommandQueue.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "SkyBodyModel.h"

namespace rs::render {

struct SkyBodyFrame {
    rs::core::Mat4 view;
    rs::core::Mat4 proj;
    rs::core::Vec3 directionToSun;   // カメラから見た太陽の方向(単位ベクトル。長さ0なら描かない)
    rs::core::Vec3 directionToMoon;  // 同・月(長さ0なら描かない)
    float landscapeLight[3] = {1.0f, 1.0f, 1.0f};  // 遠景のアンビエントに掛ける光の倍率(リニア)
};

class SkyBodyPass {
public:
    static constexpr std::uint32_t kSrvCount = 3;  // 先頭から Sun・Moon・遠景

    SkyBodyPass();
    ~SkyBodyPass();
    SkyBodyPass(const SkyBodyPass&) = delete;
    SkyBodyPass& operator=(const SkyBodyPass&) = delete;

    // device・srvHeap・queue は使い終わるまで生きていること。firstSrvIndex から kSrvCount 枠を使う。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, const rs::rhi::CommandQueue& queue,
              std::uint32_t firstSrvIndex);

    // 読み済みの Sun・Moon をGPUへ上げる(完了まで待つ)。ok でないほうは捨てる。旧資源は置き換えるので、
    // 呼び出し側が事前にGPUの完了を待つこと。
    void Upload(const SkyBodyImages& images);

    // 資源を捨てる(呼び出し側が事前にGPUの完了を待つこと)。
    void Clear();

    bool HasSun() const;
    bool HasMoon() const;
    bool HasLandscape() const;

    // 空パスの直後に記録する(呼び出し側がHDRシーンターゲットをバインド済みの前提)。上げてある天体だけ描く。
    void Record(const rs::rhi::CommandList& commandList, const SkyBodyFrame& frame) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
