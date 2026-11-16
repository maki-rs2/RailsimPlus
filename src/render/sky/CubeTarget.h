//
// CubeTarget.h - キューブマップ(6面・ミップ付き)の描画先と、そのSRV
//
// IBLの環境ソース・拡散・鏡面の入れ物。形式はR16G16B16A16_FLOAT固定。資源は1つ(配列スライス6枚
// ×ミップ)で、面×ミップごとのRTVを自前のヒープに持つ。SRVは共有SRVヒープの1枠(TextureCube、
// 全ミップ)。状態(SRV/RT)は資源全体で追跡し、Transitionは必要な遷移だけを記録する。
// 面の向き: スライス 0〜5 = +X,-X,+Y,-Y,+Z,-Z(D3D標準。方向との対応は AtmosphereMath の
// CubeFaceDirection と IblShader.h の CubeFaceDirection が同じ式)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../postprocess/SceneHdrTarget.h"
#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

// 資源の形式。Hdr16 は kSceneHdrFormat(R16G16B16A16_FLOAT)、Float32 は R32G32B32A32_FLOAT
// (夜の暗い値がfp16の最小の非正規数より小さく0に丸まるのを避ける、1x1の平均などの用)。
enum class CubeFormat { Hdr16, Float32 };

class CubeTarget {
public:
    CubeTarget();
    ~CubeTarget();
    CubeTarget(const CubeTarget&) = delete;
    CubeTarget& operator=(const CubeTarget&) = delete;

    // 資源・RTV・SRVを作る。device・srvHeap は使い終わるまで生きていること(所有しない)。
    // srvIndex は srvHeap の何番目の枠をSRVに使うか。size は1辺(ミップ0)、mipCount はミップ数。
    // 生成直後はSRV状態。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex,
              std::uint32_t size, std::uint32_t mipCount, CubeFormat format = CubeFormat::Hdr16);

    bool Ready() const;
    std::uint32_t MipCount() const;
    std::uint32_t MipSize(std::uint32_t mip) const;

    // 必要なら資源全体の状態遷移のバリアを記録する(同じ状態なら何もしない)。
    void Transition(const rs::rhi::CommandList& commandList, HdrTargetState state);

    // ミップ mip の全6面だけの状態遷移のバリアを記録する(ミップを順に作るときの、直前のミップをSRVにする用)。
    // 資源全体の状態の記録(Transition が見る値)は更新しない。連鎖が終わって全ミップが同じ状態へ
    // そろったら MarkAllMips で記録を合わせること。
    void TransitionMip(const rs::rhi::CommandList& commandList, std::uint32_t mip, HdrTargetState state);
    // 全ミップが state にそろったことを記録する(バリアは出さない)。TransitionMip の連鎖の最後に呼ぶ。
    void MarkAllMips(HdrTargetState state);

    // 面 face・ミップ mip をRTVにバインドし、ビューポートとシザーをそのミップの大きさにする
    // (クリアはしない。毎回全面を描く)。RenderTarget状態にしてから呼ぶこと。
    void BindFaceMip(const rs::rhi::CommandList& commandList, std::uint32_t face, std::uint32_t mip) const;

    // SRV(TextureCube、全ミップ)のGPUハンドル。
    std::uint64_t SrvGpuHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
