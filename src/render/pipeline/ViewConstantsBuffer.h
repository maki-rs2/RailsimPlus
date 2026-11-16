//
// ViewConstantsBuffer.h - ViewConstants を置く UPLOAD バッファ。
//
#pragma once

#include <cstdint>
#include <memory>

#include "../rhi/Device.h"
#include "ViewConstants.h"

namespace rs::render {

// ViewConstants を置く UPLOAD バッファ。フレーム(in-flight の2面)× 視点の枠の2次元で持つ。
// 書いた枠のGPUアドレスを MeshConstants::viewConstantsAddress へ入れると、全ての描画経路が b2 に張る。
class ViewConstantsBuffer {
public:
    static constexpr std::uint32_t kFrames = 2;

    ViewConstantsBuffer();
    ~ViewConstantsBuffer();
    ViewConstantsBuffer(const ViewConstantsBuffer&) = delete;
    ViewConstantsBuffer& operator=(const ViewConstantsBuffer&) = delete;

    // 1フレームあたり viewSlots 枠を確保する。
    bool Init(const rs::rhi::Device& device, std::uint32_t viewSlots);
    bool Ready() const;

    // フレーム frameIndex(% kFrames)の viewSlot 枠へ書き、そのGPUアドレスを返す。範囲外・未初期化は0。
    std::uint64_t Write(std::uint32_t frameIndex, std::uint32_t viewSlot, const ViewConstants& constants);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rs::render
