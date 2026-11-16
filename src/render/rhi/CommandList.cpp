#include "CommandList.h"

#include <d3d12.h>

namespace rs::rhi {

void CommandList::SetViewportAndScissor(float x, float y, float width, float height) const {
    const D3D12_VIEWPORT viewport = {x, y, width, height, 0.0f, 1.0f};
    const D3D12_RECT scissor = {static_cast<LONG>(x), static_cast<LONG>(y), static_cast<LONG>(x + width),
                                static_cast<LONG>(y + height)};
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
}

} // namespace rs::rhi
