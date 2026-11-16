//
// VolumetricFogDebug.h - ボリュメトリックフォグの格子を読み戻してログへ出す開発用の確認
//
// 環境変数 RS_DEBUG_VOLUMETRIC_FOG=1 のときだけ働く(起動後の最初の呼び出しで1回だけ読む)。一定のフレームごとに注入と積分の
// 結果を読み戻し、奥へ進むほど透過率が下がるか・画面の中央の透過率が式の高さフォグと合うか・霧に当たる光の範囲(影があると
// 下限が下がる)を出す。ゲームの描画経路は読み戻しに依存しない。
//
#pragma once

#include <memory>

#include "../rhi/CommandList.h"
#include "VolumetricFogPass.h"

namespace rs::render {

class VolumetricFogDebug {
public:
    VolumetricFogDebug();
    ~VolumetricFogDebug();
    VolumetricFogDebug(const VolumetricFogDebug&) = delete;
    VolumetricFogDebug& operator=(const VolumetricFogDebug&) = delete;

    static bool Enabled();

    // pass.Record の直後に毎フレーム呼ぶ。無効のときは何もしない。
    void Step(const rs::rhi::CommandList& commandList, VolumetricFogPass& pass, const VolumetricFogFrameParams& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
