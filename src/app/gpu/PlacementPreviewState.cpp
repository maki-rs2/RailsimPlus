//
// PlacementPreviewState.cpp - 配置プレビュー(情景1つ、または編成1本)のGPU状態
//
#include "PlacementPreviewState.h"

#include <cstring>

#include "GpuContext.h"

namespace rs::app {

bool PlacementPreviewState::Init(ID3D12Device* device,
                                 const rs::render::MeshPipelineSharedState& shared,
                                 ID3D12DescriptorHeap* srvHeap, UINT srvStride, UINT firstTex,
                                 UINT maxTextures, UINT firstTransBuf, UINT firstPartWorld,
                                 UINT firstPartWorldEnd, UINT maxParts) {
    return pipeline_.Init(device, shared, srvHeap, srvStride, firstTex, maxTextures,
                          firstTransBuf, firstPartWorld, firstPartWorldEnd, maxParts,
                          rs::render::GetTextureManager());
}

bool PlacementPreviewState::UploadMesh(
    GpuContext& gpu, const rs::render::Mesh& mesh,
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive) {
    gpu.WaitForGpu();
    bool ok = false;
    gpu.RunUploadCommands([&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
        mesh_ = mesh;
        ok = pipeline_.UploadMesh(device, upload, mesh_, keepAlive, "__placement_preview__");
    });
    return ok;
}

void PlacementPreviewState::SetWorlds(const std::vector<rs::core::Mat4>& worlds, UINT frameIndex) {
    std::vector<rs::render::PartXform> xforms;
    xforms.reserve(worlds.size());
    for (const rs::core::Mat4& world : worlds) {
        rs::render::PartXform xform;
        std::memcpy(xform.world, world.m, sizeof(xform.world));
        std::memcpy(xform.normalMat, world.m, sizeof(xform.normalMat));
        xform.normalMat[12] = xform.normalMat[13] = xform.normalMat[14] = 0.0f;
        xforms.push_back(xform);
    }
    pipeline_.UpdatePartWorlds(xforms, frameIndex);
}

} // namespace rs::app
