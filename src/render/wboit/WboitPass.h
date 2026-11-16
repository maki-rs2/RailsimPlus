//
// WboitPass.h - Weighted Blended OIT の蓄積バッファと合成パス
//
// 半透明を accum(RGBA16F)/reveal(R16F) の2枚へ蓄積し（MeshPipeline::RecordTransparent
// が wboit=true で描く）、全画面パスで不透明バックバッファへ載せる。順序非依存
// （McGuire & Bavoil 2013）。深度バッファは呼び出し側（App/MeshPipeline）と共有する。
// 合成の出力形式と背景コピーの形式は、シーンのHDRターゲットと同じものを内部で使う。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class SceneHdrTarget;

class WboitPass {
public:
    // 合成が読む SRV の連続枠数(t0=accum, t1=reveal, t2=合成前のシーンのコピー)。
    static constexpr std::uint32_t kSrvCount = 3;

    WboitPass();
    ~WboitPass();
    WboitPass(const WboitPass&) = delete;
    WboitPass& operator=(const WboitPass&) = delete;

    // srvHeap は shader-visible。合成PSが読む SRV を accumSrvIndex から kSrvCount 枠へ作る
    // （連続配置。合成のルートテーブルが accumSrvIndex を先頭に張る）。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
              std::uint32_t accumSrvIndex);

    // accum/reveal リソース・RTV・SRV を (再)生成する。Init 後と OnResize で呼ぶ。
    bool CreateTargets(const rs::rhi::Device& device, std::uint32_t width, std::uint32_t height);

    // 半透明蓄積の開始: accum/reveal を RENDER_TARGET へ遷移し、深度(depthDsv、
    // 読み取り専用テスト)と合わせて MRT バインドし、accum=0 / reveal=1 でクリアする。
    // 呼び出し後、MeshPipeline::RecordTransparent(..., wboit=true) を積む。
    void Begin(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor depthDsv);

    // 蓄積の終了と合成: accum/reveal を SRV へ遷移し、backBufferRtv を単独RTへ
    // バインドして全画面三角形で合成する（深度なし）。合成の直前に sceneTarget(backBufferRtv の
    // 実体)を内部のコピーへ写し、背景としてシェーダで読む。sceneTarget の状態は呼び出し前後で
    // 変わらない(RenderTarget のまま)。srvHeap は呼び出し側で SetDescriptorHeaps 済みであること。
    void Composite(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor backBufferRtv,
                   SceneHdrTarget& sceneTarget);

    bool Ready() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
