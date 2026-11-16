//
// ComputePipeline.h - compute シェーダ1本ぶんのルート署名と PSO
//
// ルート署名は 32bit 定数(b0)と、任意でルート CBV(b1)。資源は ResourceDescriptorHeap[] から番号で引く(描画パスと同じ
// bindless の形)。静的サンプラはメッシュのシェーダと同じ番号で、s1=リニア・クランプ、s2=影の比較サンプラ。
// 実行はグラフィックスキューのコマンドリストに積む。compute の PSO を積むと、グラフィックスの PSO は
// 差し替わるので、後続の描画パスは自分の PSO を積むこと(グラフィックスのルート署名は別枠で、影響しない)。
//
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class ComputePipeline {
public:
    ComputePipeline();
    ~ComputePipeline();
    ComputePipeline(const ComputePipeline&) = delete;
    ComputePipeline& operator=(const ComputePipeline&) = delete;

    // source の entry を cs_6_6 でコンパイルして PSO を作る。constantDwordCount はルート定数の DWORD 数(0 なら定数なし)。
    // logTag はエラーログの接頭辞。useRootCbv が真なら b1 にルート CBV を持つ(SetRootCbv で積む)。
    bool Init(const rs::rhi::Device& device, const std::string& source, const wchar_t* entry,
              std::uint32_t constantDwordCount, const char* logTag, bool useRootCbv = false);

    bool Ready() const;

    // ヒープ・ルート署名・PSO を積む。Dispatch の前に呼ぶ。
    void Bind(const rs::rhi::CommandList& commandList, const rs::rhi::SrvHeap& srvHeap) const;
    // ルート定数を積む。constants は Init で渡した DWORD 数ぶん読める領域。
    void SetConstants(const rs::rhi::CommandList& commandList, const void* constants) const;
    // b1 のルート CBV を積む。gpuAddress は定数バッファの GPU アドレス。
    void SetRootCbv(const rs::rhi::CommandList& commandList, std::uint64_t gpuAddress) const;
    void Dispatch(const rs::rhi::CommandList& commandList, std::uint32_t groupsX, std::uint32_t groupsY,
                  std::uint32_t groupsZ) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
