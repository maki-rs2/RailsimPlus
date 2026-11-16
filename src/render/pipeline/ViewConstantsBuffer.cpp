#include "ViewConstantsBuffer.h"
#include "ViewConstants.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "MeshPipelineTypes.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

struct ViewConstantsBuffer::Impl {
    ComPtr<ID3D12Resource> buffers[kFrames];
    std::uint8_t* mapped[kFrames] = {};
    std::uint32_t viewSlots = 0;
};

ViewConstantsBuffer::ViewConstantsBuffer() = default;
ViewConstantsBuffer::~ViewConstantsBuffer() = default;

bool ViewConstantsBuffer::Init(const rs::rhi::Device& device, std::uint32_t viewSlots) {
    impl_.reset();
    if (!device.Valid() || viewSlots == 0) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->viewSlots = viewSlots;
    for (std::uint32_t frame = 0; frame < kFrames; ++frame) {
        if (!CreatePooledUploadBuffer(device.Native(), sizeof(ViewConstants) * viewSlots, impl->buffers[frame])) {
            spdlog::error("[view_constants] 視点の定数バッファの確保に失敗しました");
            return false;
        }
        const D3D12_RANGE noRead = {0, 0};
        if (FAILED(impl->buffers[frame]->Map(0, &noRead, reinterpret_cast<void**>(&impl->mapped[frame])))) {
            spdlog::error("[view_constants] 視点定数バッファのMapに失敗しました frame={}", frame);
            return false;
        }
    }
    impl_ = std::move(impl);
    return true;
}

bool ViewConstantsBuffer::Ready() const { return impl_ != nullptr; }

std::uint64_t ViewConstantsBuffer::Write(std::uint32_t frameIndex, std::uint32_t viewSlot,
                                         const ViewConstants& constants) {
    if (!impl_ || viewSlot >= impl_->viewSlots) return 0;
    const std::uint32_t frame = frameIndex % kFrames;
    std::memcpy(impl_->mapped[frame] + sizeof(ViewConstants) * viewSlot, &constants, sizeof(ViewConstants));
    return impl_->buffers[frame]->GetGPUVirtualAddress() + sizeof(ViewConstants) * viewSlot;
}

} // namespace rs::render
