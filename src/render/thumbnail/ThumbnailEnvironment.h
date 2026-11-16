//
// ThumbnailEnvironment.h - サムネイル撮影用の固定の環境光(大気LUT+IBL)
//
// ThumbnailRenderer が専用SRVヒープに置く、AtmosphereLuts と IblGenerator の自前インスタンスの束。
// 本編のキューブは別スレッド・別キュー・別ヒープなので読まず、昼の太陽高度で1回だけ生成して
// 撮影のあいだ固定で使う。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/Device.h"
#include "../rhi/SrvHeap.h"
#include "../sky/EnvironmentState.h"

namespace rs::render {

class ThumbnailEnvironment {
public:
    // 大気LUT3枠+IBL5枠+月用のSky-View LUT1枠+暗順応の結果1枠。呼び出し側(SRVヒープの大きさの計算)が使う。
    static std::uint32_t SrvCount();

    ThumbnailEnvironment();
    ~ThumbnailEnvironment();
    ThumbnailEnvironment(const ThumbnailEnvironment&) = delete;
    ThumbnailEnvironment& operator=(const ThumbnailEnvironment&) = delete;

    // firstSrvIndex から SrvCount() 枠を使う。device・srvHeap は使い終わるまで生きていること。
    bool Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex);

    // 開いているコマンドリストへ、固定の太陽高度での大気LUT→IBLの生成を記録する。
    // 記録したら呼び出し側が Close・実行・完了待ちをすること。生成が済むと Ready() が真になる。
    void RecordGeneration(const rs::rhi::CommandList& commandList);

    // 生成が済んでいて、メッシュのシェーダが引ける状態。
    bool Ready() const;

    // メッシュのシェーダが引くSRVの絶対番号。
    EnvironmentSrvIndices SrvIndices() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
