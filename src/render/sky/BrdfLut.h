//
// BrdfLut.h - split-sum IBLのBRDF LUT(R16G16_FLOAT、2D)の生成と保持
//
// 横=NdotV、縦=roughness(上が0)、RG=(A,B)。環境に依存しないので、起動後に1回だけ生成する。
// SRVは共有SRVヒープの1枠。生成後はSRV状態。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class BrdfLut {
public:
    BrdfLut();
    ~BrdfLut();
    BrdfLut(const BrdfLut&) = delete;
    BrdfLut& operator=(const BrdfLut&) = delete;

    // 資源・PSO・SRVを作る。device・srvHeap は使い終わるまで生きていること。srvIndex はSRVの枠。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex);

    bool Ready() const;
    bool Generated() const;

    // 全面を描いて、SRV状態へ戻す。2回目以降は何もしない。RTV・ビューポート・シザーを書き換える
    // (呼び出し側が張り直すこと)。記録したら true。
    bool Generate(const rs::rhi::CommandList& commandList);

    std::uint64_t SrvGpuHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
