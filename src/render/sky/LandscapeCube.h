//
// LandscapeCube.h - 標準の空の組み込み遠景のキューブ(GPU資源)
//
// assets/sky/Landscape.png(帯の画像)を読み、LandscapeMesh の
// 帯を原点から6方向(90度FOV)に描いて、RGBAのキューブ(1面1024²・ミップ付き・R16G16B16A16_FLOAT)にする。
// 値は「アルベド×カバレッジ(プリマルチプライ)、カバレッジ」で、光は掛けない(光は SampleLandscape が掛ける)。
// 画像は Init で読んで上げ、焼き込み(Bake)は起動後の1回だけ記録する。画像が読めなければ
// Available() が偽のまま(遠景なしで動く)。面の向きは LandscapeCubeFaceBasis と同じ。
//
// SRVは共有SRVヒープの1枠(TextureCube、全ミップ)。焼き込みの入力(元画像・ミップごとのSRV)は
// 自前のシェーダ可視ヒープに置く(共有ヒープの枠を増やさない)。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CommandQueue.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "LandscapeContrast.h"

namespace rs::render {

class LandscapeCube {
public:
    // キューブの1辺(ミップ0)。画像の幅4096pxが方位360度なので、90度ぶんの1面が1024pxで画像と釣り合う。
    static constexpr std::uint32_t kFaceSize = 1024;

    LandscapeCube();
    ~LandscapeCube();
    LandscapeCube(const LandscapeCube&) = delete;
    LandscapeCube& operator=(const LandscapeCube&) = delete;

    // 画像を読んで上げ(完了を待つ)、資源・PSOを作る。device・srvHeap・queue は使い終わるまで生きていること。
    // srvIndex は共有ヒープの1枠。画像が読めない・作れないときは false(Available は偽のまま)。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
              const rs::rhi::CommandQueue& queue, std::uint32_t srvIndex);

    // 画像が読めて、資源が作れた。
    bool Available() const;

    // 焼き込みを記録する(Available で、まだ焼いていないときだけ。記録したら true)。
    // RTV・ビューポート・シザー・ディスクリプタヒープを書き換えるので、呼び出し側が張り直すこと。
    // 終わると全ミップがSRV状態。以降 Ready() が真になる。
    bool Bake(const rs::rhi::CommandList& commandList);

    // 焼き込みを記録済み(同じコマンドリスト以降で引いてよい)。
    bool Ready() const;

    // 共有ヒープ内のSRVのGPUハンドル。Init 前は0。
    std::uint64_t SrvGpuHandle() const;

    // 画像の明暗の幅の統計(Init で、調整値 landscapeContrast に合わせて1回だけ計算)。Init 前は既定値。
    LandscapeContrastStats ContrastStats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
