//
// FrameHighlightState.cpp - シーン選択ハイライトのGPU状態
//
#include "FrameHighlightState.h"

#include <cstring>
#include <future>

#include "GpuContext.h"

namespace rs::app {

bool FrameHighlightState::Init(
    ID3D12Device* device, const rs::render::MeshPipelineSharedState& shared,
    ID3D12DescriptorHeap* srvHeap, UINT srvStride,
    const std::array<PipelineInit, kHighlightSlots>& pipelineInits,
    UINT maxTextures, UINT maxParts) {
    for (UINT i = 0; i < kHighlightSlots; ++i) {
        const PipelineInit& pipelineInit = pipelineInits[i];
        if (!highlightPipelines_[i].Init(device, shared, srvHeap, srvStride,
                                         pipelineInit.firstTex, maxTextures,
                                         pipelineInit.firstTransBuf, pipelineInit.firstPartWorld,
                                         pipelineInit.firstPartWorldEnd, maxParts,
                                         rs::render::GetTextureManager())) {
            return false;
        }
        highlightActive_[i] = false;
        highlightWorld_[i] = rs::core::Mat4::Identity();
    }
    return true;
}

bool FrameHighlightState::UploadMesh(
    GpuContext& gpu, UINT which, const rs::render::Mesh& mesh,
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive) {
    if (which >= kHighlightSlots) return false;

    gpu.WaitForGpu();
    // 前に輪郭を出したもののテクスチャをプールから外す(理由は PlacementPreviewState::UploadMesh と同じ)。
    highlightPipelines_[which].Reset();
    keepAlive.clear();
    gpu.ResetCmdList();
    DirectX::ResourceUploadBatch upload(gpu.Device());
    upload.Begin();
    const bool ok = highlightPipelines_[which].UploadMesh(gpu.Device(), upload, mesh, keepAlive);
    gpu.CloseAndExecuteCmdList();
    std::future<void> uploadFinished = upload.End(gpu.Queue());
    uploadFinished.wait();
    gpu.WaitForGpu();
    keepAlive.clear();
    return ok;
}

void FrameHighlightState::SetActive(UINT which, bool active, const rs::core::Mat4& world,
                                    const float tintColor[4]) {
    if (which >= kHighlightSlots) return;
    highlightActive_[which] = active;
    if (!active) return;
    highlightWorld_[which] = world;
    std::memcpy(highlightTint_[which], tintColor, sizeof(highlightTint_[which]));
}

void FrameHighlightState::SetSlotHighlight(UINT which, bool active, UINT slot,
                                           const float tintColor[4]) {
    if (which >= kHighlightSlots) return;
    slotHighlightActive_[which] = active;
    if (!active) return;
    slotHighlightSlot_[which] = slot;
    std::memcpy(slotHighlightTint_[which], tintColor, sizeof(slotHighlightTint_[which]));
}

bool FrameHighlightState::Active(UINT which) const {
    return which < kHighlightSlots && highlightActive_[which];
}

const rs::core::Mat4* FrameHighlightState::World(UINT which) const {
    return which < kHighlightSlots ? &highlightWorld_[which] : nullptr;
}

const float* FrameHighlightState::Tint(UINT which) const {
    return which < kHighlightSlots ? highlightTint_[which] : nullptr;
}

rs::render::MeshPipeline* FrameHighlightState::Pipeline(UINT which) {
    return which < kHighlightSlots ? &highlightPipelines_[which] : nullptr;
}

bool FrameHighlightState::SlotHighlightActive(UINT which) const {
    return which < kHighlightSlots && slotHighlightActive_[which];
}

UINT FrameHighlightState::SlotHighlightSlot(UINT which) const {
    return which < kHighlightSlots ? slotHighlightSlot_[which] : 0;
}

const float* FrameHighlightState::SlotHighlightTint(UINT which) const {
    return which < kHighlightSlots ? slotHighlightTint_[which] : nullptr;
}

} // namespace rs::app
