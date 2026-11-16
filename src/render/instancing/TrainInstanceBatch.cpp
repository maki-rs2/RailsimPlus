#include "TrainInstanceBatch.h"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

namespace rs::render {

bool TrainInstanceBatch::Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
                                ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT firstTexIndex,
                                UINT maxTextures, UINT firstTransBufIndex, UINT firstPartWorldIndex,
                                UINT initialInstances) {
    device_ = device;
    return pipeline_.Init(device, shared, srvHeap, srvSize, firstTexIndex, maxTextures,
                          firstTransBufIndex, firstPartWorldIndex, firstPartWorldIndex + 2, initialInstances,
                          GetTextureManager());
}

bool TrainInstanceBatch::UploadModel(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                       const std::string& contentKey, const Mesh& mesh,
                                       UINT partsPerInstance) {
    if (partsPerInstance == 0) {
        spdlog::warn("[train_instance_batch] UploadModel: partsPerInstance=0 のため中止 key={}",
                    contentKey);
        return false;
    }
    partsPerInstance_ = partsPerInstance;
    float maxDistSq = 0.0f;
    for (const MeshVertex& vertex : mesh.vertices) {
        const float distSq = vertex.pos[0] * vertex.pos[0] + vertex.pos[1] * vertex.pos[1] +
                             vertex.pos[2] * vertex.pos[2];
        maxDistSq = std::max(maxDistSq, distSq);
    }
    boundRadius_ = std::sqrt(maxDistSq);
    return pipeline_.UploadMesh(device, upload, mesh, keepAlive_, contentKey);
}

void TrainInstanceBatch::ConfigureDynamicMaterialSlots(
    const std::vector<uint32_t>& dynamicMaterialIndices) {
    dynamicMaterialSlotCount_ = static_cast<UINT>(dynamicMaterialIndices.size());
    pipeline_.ConfigureDynamicMaterialSlots(dynamicMaterialIndices);
}

bool TrainInstanceBatch::ApplyPreloadedTextureOverride(
    const std::string& basePath, const std::string& normalPath, const std::string& pbrPath,
    InstanceMatOverride& materialOverride) const {
    PreloadedTexInfo info;
    if (!pipeline_.FindPreloadedSlot(basePath, normalPath, pbrPath, info)) return false;
    pipeline_.ApplyInstanceMaterialTexture(materialOverride, true, info);
    return true;
}

bool TrainInstanceBatch::PreloadTextureOverride(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& basePath, const std::string& normalPath, const std::string& pbrPath,
    InstanceMatOverride& materialOverride) {
    PreloadedTexInfo info;
    if (!pipeline_.PreloadTexture(device, upload, basePath, normalPath, pbrPath, info)) return false;
    pipeline_.ApplyInstanceMaterialTexture(materialOverride, true, info);
    return true;
}

bool TrainInstanceBatch::TextureUnavailable(const std::string& basePath, const std::string& normalPath,
                                            const std::string& pbrPath) const {
    return pipeline_.TexKnownFailed(basePath) ||
           pipeline_.TexBudgetExhausted(basePath, normalPath, pbrPath);
}

bool TrainInstanceBatch::AddInstance(
    const std::vector<PartXform>& partWorlds,
    std::span<const InstanceMatOverride> materialOverrides, uint64_t instanceKey) {
    if (partWorlds.size() != partsPerInstance_) {
        spdlog::warn("[train_instance_batch] part count mismatch: expected {}, got {}", partsPerInstance_, partWorlds.size());
        return false;
    }
    if (materialOverrides.size() != dynamicMaterialSlotCount_) {
        spdlog::warn("[train_instance_batch] material override count mismatch: expected {}, got {}",
                     dynamicMaterialSlotCount_, materialOverrides.size());
        return false;
    }
    instances_.insert(instances_.end(), partWorlds.begin(), partWorlds.end());
    materialOverrides_.insert(materialOverrides_.end(), materialOverrides.begin(), materialOverrides.end());
    ++instanceCount_;
    instanceFingerprint_ = CombineContinuityKey(instanceFingerprint_, instanceKey);
    for (bool& dirty : partWorldDirty_) dirty = true;
    return true;
}

void TrainInstanceBatch::ClearInstances() {
    instances_.clear();
    materialOverrides_.clear();
    instanceCount_ = 0;
    instanceFingerprint_ = kContinuitySeed;
    for (bool& dirty : partWorldDirty_) dirty = true;
}

void TrainInstanceBatch::Sync(UINT frameIndex) {
    // 車両のパーツ姿勢は毎フレーム変わることがある。現在フレームが参照するSRVだけを
    // 更新し、GPUが前フレームを読むもう一方のバッファには触れない。
    const UINT frame = frameIndex % kFrames;
    if (instances_.size() > static_cast<size_t>(UINT_MAX)) {
        spdlog::error("[train_instance_batch] part transform count exceeds UINT range");
        return;
    }
    if (materialOverrides_.size() > static_cast<size_t>(UINT_MAX)) {
        spdlog::error("[train_instance_batch] material override count exceeds UINT range");
        return;
    }
    if (!pipeline_.EnsurePartWorldCapacity(device_, UINT(instances_.size()), frameIndex)) {
        partWorldReady_[frame] = false;
        return;
    }
    pipeline_.UpdatePartWorlds(instances_, frameIndex, instanceFingerprint_);
    if (!pipeline_.EnsureInstanceMatOverrideCapacity(device_, UINT(materialOverrides_.size()), frameIndex)) {
        partWorldReady_[frame] = false;
        return;
    }
    pipeline_.UpdateInstanceMatOverrides(materialOverrides_, frameIndex);
    partWorldDirty_[frame] = false;
    partWorldReady_[frame] = true;
}

void TrainInstanceBatch::RecordOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames]) return;
    pipeline_.RecordInstancedMultiPart(cmdList, base, frameIndex, UINT(instanceCount_), partsPerInstance_);
}

void TrainInstanceBatch::RecordBlended(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames]) return;
    pipeline_.RecordBlendedInstancedMultiPart(cmdList, base, frameIndex, UINT(instanceCount_), partsPerInstance_);
}

void TrainInstanceBatch::RecordLateOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames]) return;
    pipeline_.RecordLateOpaqueInstancedMultiPart(cmdList, base, frameIndex, UINT(instanceCount_), partsPerInstance_);
}

bool TrainInstanceBatch::AnyInstanceInFrustum(const rs::core::Frustum& frustum) const {
    for (const PartXform& xform : instances_) {
        const float* w = xform.world; // row-vector 慣習: 平行移動は w[12..14]、各軸のスケールは行の長さ
        const float scaleSq = std::max({w[0] * w[0] + w[1] * w[1] + w[2] * w[2],
                                        w[4] * w[4] + w[5] * w[5] + w[6] * w[6],
                                        w[8] * w[8] + w[9] * w[9] + w[10] * w[10]});
        const float radius = boundRadius_ * std::sqrt(scaleSq);
        bool inside = true;
        for (const float (&plane)[4] : frustum.planes) {
            if (plane[0] * w[12] + plane[1] * w[13] + plane[2] * w[14] + plane[3] < -radius) {
                inside = false;
                break;
            }
        }
        if (inside) return true;
    }
    return false;
}

void TrainInstanceBatch::RecordShadow(const rs::rhi::CommandList& commandList,
                                      const MeshConstants& lightConstants, UINT frameIndex,
                                      UINT shadowSlice, const rs::core::Frustum* lightFrustum) {
    if (!partWorldReady_[frameIndex % kFrames] || instanceCount_ == 0) return;
    if (lightFrustum != nullptr && !AnyInstanceInFrustum(*lightFrustum)) return;
    pipeline_.RecordShadowInstancedMultiPart(commandList, lightConstants, frameIndex, shadowSlice,
                                             UINT(instanceCount_), partsPerInstance_);
}

void TrainInstanceBatch::RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base,
                                       UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames] || instanceCount_ == 0) return;
    pipeline_.RecordPrepassInstancedMultiPart(commandList, base, frameIndex, UINT(instanceCount_), partsPerInstance_);
}

void TrainInstanceBatch::RecordVelocity(const rs::rhi::CommandList& commandList, const MeshConstants& base,
                                        UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames] || instanceCount_ == 0) return;
    pipeline_.RecordVelocityInstancedMultiPart(commandList, base, frameIndex, UINT(instanceCount_), partsPerInstance_);
}

void TrainInstanceBatch::RecordTransparent(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) {
    if (!partWorldReady_[frameIndex % kFrames]) return;
    pipeline_.RecordTransparentInstancedMultiPart(cmdList, base, frameIndex, UINT(instanceCount_), partsPerInstance_, true);
}

} // namespace rs::render
