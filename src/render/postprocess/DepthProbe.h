//
// DepthProbe.h - 深度バッファの1画素を CPU へ読み戻す(フォトモードのクリックでフォーカス用)
//
// Record で深度の1画素を読み戻し用バッファへ写し、数回の Poll のあとに値を取り出す。ゲームの描画は読み戻しを待たない
// (ExposureTarget の読み戻しと同じ、フレーム数で待つ方式)。DX12 の型は .cpp に閉じる。
//
#pragma once

#include <memory>

#include "../rhi/CommandList.h"

namespace rs::render {

class DepthProbe {
public:
    DepthProbe();
    ~DepthProbe();
    DepthProbe(const DepthProbe&) = delete;
    DepthProbe& operator=(const DepthProbe&) = delete;
    DepthProbe(DepthProbe&&) noexcept;
    DepthProbe& operator=(DepthProbe&&) noexcept;

    // 深度バッファ(書き込み状態。R32 系。nativeDepth は深度バッファの実体へのポインタ)の画素(x, y)を写す。記録の前後で深度の状態は変わらない。
    // すでに読み戻し待ちのときは何もしない。写せたら true。
    bool Record(const rs::rhi::CommandList& commandList, void* nativeDepth, unsigned x, unsigned y);

    // 写したあと、GPU が書き終えるまでの数回の呼び出しを数え、揃ったら値を返す。
    bool Poll(float& depth);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
