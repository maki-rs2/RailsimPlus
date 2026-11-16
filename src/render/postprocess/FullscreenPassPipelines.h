//
// FullscreenPassPipelines.h - 全画面の三角形を描くピクセルシェーダのパスが共通で使う部品
//
// ルート署名(32bit 定数 b0 だけ。SRV はディスクリプタヒープから番号で直接引く)、
// 頂点シェーダ(VSMain)のコンパイル、ピクセルシェーダごとのパイプライン、全画面の三角形の描画をまとめる。
// 深度なし・カリングなし・描画先1枚。ポストプロセスのうち、画素ごとに Load で読むパスが使う。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "SceneHdrTarget.h"

namespace rs::render {

class FullscreenPassPipelines {
public:
    FullscreenPassPipelines();
    ~FullscreenPassPipelines();
    FullscreenPassPipelines(const FullscreenPassPipelines&) = delete;
    FullscreenPassPipelines& operator=(const FullscreenPassPipelines&) = delete;

    // ルート署名と頂点シェーダ(エントリ VSMain)を用意する。shaderSource はパスの HLSL 全体。
    // tag はログとシェーダ名に使う。constantDwordCount は 32bit 定数の個数。
    // srvHeap は使い終わるまで生きていること(コピーを持つが、中身は所有しない)。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::string_view shaderSource,
              const char* tag, std::uint32_t constantDwordCount);

    // entry のピクセルシェーダでパイプラインを作る。format は描画先の形式。
    // 追加した順に 0 から番号が付き、Draw の pixelShaderIndex に使う。
    bool AddPixelShader(const wchar_t* entry, HdrTargetFormat format);

    // ディスクリプタヒープ・ルート署名・トポロジ・32bit 定数を設定する(Init の constantDwordCount 個ぶん)。
    void Bind(const rs::rhi::CommandList& commandList, const void* constants);

    // target を書き込み先にして(クリアする。RenderTarget 状態へ遷移する)、
    // target と同じ大きさの全画面の三角形を、pixelShaderIndex 番のシェーダで1回描く。
    void Draw(const rs::rhi::CommandList& commandList, SceneHdrTarget& target, std::size_t pixelShaderIndex);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
