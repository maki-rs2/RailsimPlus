//
// TonemapPass.h - HDRシーンターゲットをバックバッファ向けのLDRへ変換するフルスクリーンパス
//
// 本編のバックバッファ(sRGB RTV)とサムネイルの撮影用RT(R8G8B8A8_UNORM_SRGB)は
// 出力形式が同じ(kTonemapOutputFormat)なので、PSO・ルート署名を1つ作って共用できる。
// Init後の記録(Record)は状態を変えない const 関数で、別スレッド・別コマンドリストから
// 同時に呼んでよい。SetParamsも記録と同時に呼んでよい(設定の読み書きは鍵で守っている)。
// シェーダ出力はリニア値で、sRGBへの変換は出力先のRTVが行う。
//
#pragma once

#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/CpuDescriptor.h"
#include "../rhi/Device.h"
#include "OutputRect.h"
#include "SceneHdrTarget.h"
#include "TonemapMath.h"

namespace rs::render {

class TonemapPass {
public:
    TonemapPass();
    ~TonemapPass();
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    // PSO・ルート署名を作る。params は以後Recordが使う設定(本編とサムネイルで共通)。
    bool Init(const rs::rhi::Device& device, const TonemapParams& params);

    bool Ready() const;

    // 設定を差し替える。別スレッドの記録(サムネイルの撮影)と同時に呼んでよい。
    void SetParams(const TonemapParams& params);
    // 別スレッドからの SetParams と競合しないよう、参照ではなくコピーを返す。
    TonemapParams Params() const;

    // source のSRVを読み、outputRtv(kTonemapOutputFormat)へ全画面三角形で書く。
    // source は ShaderResource 状態であること。ビューポート・シザーは呼び出し側が設定済みの前提。
    // source のSRVヒープをバインドし直す。
    void Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                rs::rhi::CpuDescriptor outputRtv) const;
    // 設定を引数で上書きして記録する(保持している設定は変えない)。プラグインの空のパススルー用。
    // outRect を渡すと、入力の全体を出力先のその矩形へ引き伸ばして書く(ビューポートも矩形に合わせておく)。
    void Record(const rs::rhi::CommandList& commandList, const SceneHdrTarget& source,
                rs::rhi::CpuDescriptor outputRtv, const TonemapParams& params,
                const OutputRect* outRect = nullptr) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
