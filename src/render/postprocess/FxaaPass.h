//
// FxaaPass.h - FXAA(トーンマップ後のLDRに掛ける輪郭のアンチエイリアス)のフルスクリーンパス
//
// 入力はトーンマップ後のLDR(HdrTargetFormat::Srgb8 の SceneHdrTarget)、出力は
// kTonemapOutputFormat のRTV(本編ではバックバッファ)。輪郭でない画素は入力をそのまま書く。
//
#pragma once

#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "OutputRect.h"
#include "SceneHdrTarget.h"

namespace rs::render {

class FxaaPass {
public:
    FxaaPass();
    ~FxaaPass();
    FxaaPass(const FxaaPass&) = delete;
    FxaaPass& operator=(const FxaaPass&) = delete;

    // PSO・ルート署名を作る。
    bool Init(const rs::rhi::Device& device);

    bool Ready() const;

    // source を読み、outputRtv へ全画面三角形で書く。source は ShaderResource 状態であること。
    // ビューポート・シザーは呼び出し側が設定済みの前提(outRect が無ければ source と同じ大きさ)。
    // outRect を渡すと、source の全体を出力先のその矩形へ引き伸ばして書く(ビューポートも矩形に合わせておく)。
    // source のSRVヒープをバインドし直す。
    void Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                rs::rhi::CpuDescriptor outputRtv, const OutputRect* outRect = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
