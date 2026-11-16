//
// PlacementPreviewState.h - 配置プレビュー(情景1つ、または編成1本)のGPU状態
//
#pragma once

#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "../../core/Math.h"
#include "../../render/mesh/Mesh.h"
#include "../../render/pipeline/MeshPipeline.h"

namespace rs::app {

class GpuContext;

class PlacementPreviewState {
public:
    bool Init(ID3D12Device* device, const rs::render::MeshPipelineSharedState& shared,
              ID3D12DescriptorHeap* srvHeap, UINT srvStride, UINT firstTex, UINT maxTextures,
              UINT firstTransBuf, UINT firstPartWorld, UINT firstPartWorldEnd, UINT maxParts);
    bool UploadMesh(GpuContext& gpu, const rs::render::Mesh& mesh,
                    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive);
    // パーツ(partIndex)ごとのワールド行列を設定する。情景は要素1つ、編成は1両につき1つ。
    void SetWorlds(const std::vector<rs::core::Mat4>& worlds, UINT frameIndex);
    void SetActive(bool active) { active_ = active; }

    bool Active() const { return active_; }
    rs::render::MeshPipeline& Pipeline() { return pipeline_; }

private:
    rs::render::MeshPipeline pipeline_;
    rs::render::Mesh mesh_;
    bool active_ = false;
};

} // namespace rs::app
