//
// FrameHighlightState.h - シーン選択ハイライトのGPU状態
//
#pragma once

#include <array>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "../../core/Math.h"
#include "../../render/mesh/Mesh.h"
#include "../../render/pipeline/MeshPipeline.h"

namespace rs::app {

class GpuContext;

class FrameHighlightState {
public:
    static constexpr UINT kHighlightSlots = 2;

    struct PipelineInit {
        UINT firstTex;
        UINT firstTransBuf;
        UINT firstPartWorld;
        UINT firstPartWorldEnd;
    };

    bool Init(ID3D12Device* device, const rs::render::MeshPipelineSharedState& shared,
              ID3D12DescriptorHeap* srvHeap, UINT srvStride,
              const std::array<PipelineInit, kHighlightSlots>& pipelineInits,
              UINT maxTextures, UINT maxParts);
    bool UploadMesh(GpuContext& gpu, UINT which, const rs::render::Mesh& mesh,
                    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive);
    void SetActive(UINT which, bool active, const rs::core::Mat4& world,
                   const float tintColor[4]);
    void SetSlotHighlight(UINT which, bool active, UINT slot, const float tintColor[4]);

    bool Active(UINT which) const;
    const rs::core::Mat4* World(UINT which) const;
    const float* Tint(UINT which) const;
    rs::render::MeshPipeline* Pipeline(UINT which);
    bool SlotHighlightActive(UINT which) const;
    UINT SlotHighlightSlot(UINT which) const;
    const float* SlotHighlightTint(UINT which) const;

private:
    rs::render::MeshPipeline highlightPipelines_[kHighlightSlots];
    bool highlightActive_[kHighlightSlots] = {false, false};
    rs::core::Mat4 highlightWorld_[kHighlightSlots];
    float highlightTint_[kHighlightSlots][4] = {};
    bool slotHighlightActive_[kHighlightSlots] = {false, false};
    UINT slotHighlightSlot_[kHighlightSlots] = {0, 0};
    float slotHighlightTint_[kHighlightSlots][4] = {};
};

} // namespace rs::app
