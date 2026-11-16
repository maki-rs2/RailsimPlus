//
// ComputeSelfTest.h - compute の基盤が実機で動くかを確かめる開発用の確認
//
// 環境変数 RS_DEBUG_COMPUTE=1 のときだけ働く。1枚目の3Dテクスチャへ座標から決まる値を UAV で書き、それを SRV で読んで
// 2倍にした値を2枚目へ書く。2枚目を読み戻して全テクセルを期待値と比べ、結果をログへ出す。起動ごとに1回だけ走る。
// フレームの途中で走らせるので、後続の描画が compute の PSO に影響されないかも合わせて確かめられる。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"

namespace rs::render {

class ComputeSelfTest {
public:
    static constexpr std::uint32_t kSrvCount = 4; // 3Dテクスチャ2枚の SRV・UAV

    ComputeSelfTest();
    ~ComputeSelfTest();
    ComputeSelfTest(const ComputeSelfTest&) = delete;
    ComputeSelfTest& operator=(const ComputeSelfTest&) = delete;

    static bool Enabled();

    // 無効のときは何もしない。
    void Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);
    // 毎フレーム呼ぶ。最初の回に記録し、数フレーム後に読み戻して判定する。
    void Step(const rs::rhi::CommandList& commandList);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
