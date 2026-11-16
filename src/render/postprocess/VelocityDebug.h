//
// VelocityDebug.h - 速度バッファの中身を読み戻してログへ出す開発用の確認
//
// 環境変数 RS_DEBUG_VELOCITY=1 のときだけ働く(起動後の最初の呼び出しで1回だけ読む)。速度パスもこの変数で有効になる。
// 一定のフレームごとに速度 RT を読み戻し用バッファへ写し、数フレーム後に取り出して、動きが書かれた画素の数・最大の大きさ・
// 範囲をログへ出す。ゲームの描画経路は読み戻しに依存しない。
//
#pragma once

#include <memory>

#include "../rhi/CommandList.h"
#include "SceneHdrTarget.h"

namespace rs::render {

class VelocityDebug {
public:
    VelocityDebug();
    ~VelocityDebug();
    VelocityDebug(const VelocityDebug&) = delete;
    VelocityDebug& operator=(const VelocityDebug&) = delete;

    static bool Enabled();

    // 速度パスを記録した直後に毎フレーム呼ぶ。velocity は ShaderResource 状態であること。無効のときは何もしない。
    void Step(const rs::rhi::CommandList& commandList, SceneHdrTarget& velocity);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
