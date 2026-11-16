#include "InstanceBatchManager.h"

#include <cstring>

#include "../resource/RenderTuning.h"
#include "../resource/TextureManager.h"

namespace rs::render {

bool InstanceBatchManager::Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
                                ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                                UINT firstSrvIndex, UINT srvPoolCapacity,
                                UINT batchMaxTextures, UINT batchInitialInstances) {
    failedBatchKeys_.clear();
    return impl_.Init(device, shared, srvHeap, srvSize, firstSrvIndex, srvPoolCapacity,
                      batchMaxTextures, batchInitialInstances);
}

bool InstanceBatchManager::EnsureBatch(ID3D12Device* device,
                                       DirectX::ResourceUploadBatch& upload,
                                       const std::string& basePath, const Mesh& mesh) {
    if (impl_.HasBatch(basePath)) return true;
    if (failedBatchKeys_.contains(basePath)) return false;
    if (!impl_.EnsureBatch(device, upload, basePath, mesh, 1, {})) {
        failedBatchKeys_.insert(basePath);
        return false;
    }
    return true;
}

void InstanceBatchManager::CollectNewBatchTexturePaths(const std::string& basePath,
                                                       const Mesh& mesh,
                                                       std::vector<std::string>& out) const {
    if (impl_.HasBatch(basePath) || failedBatchKeys_.contains(basePath)) return;
    TextureManager& textureManager = GetTextureManager(); // バッチのパイプラインと同じ取得先
    for (const Material& mat : mesh.materials) {
        if (mat.texturePath.empty()) continue;
        if (textureManager.IsKnownFailed(mat.texturePath)) continue;
        if (textureManager.HasResident(mat.texturePath, mat.normalPath, mat.ormPath)) continue;
        out.push_back(mat.texturePath);
        if (!mat.normalPath.empty()) out.push_back(mat.normalPath);
        if (!mat.ormPath.empty()) out.push_back(mat.ormPath);
    }
}

bool InstanceBatchManager::AddInstance(const std::string& basePath, const float world[16]) {
    PartXform partTransform{};
    std::memcpy(partTransform.world, world, sizeof(partTransform.world));
    std::memcpy(partTransform.normalMat, world, sizeof(partTransform.normalMat));
    // 静的な配置なので、識別は行列そのもの(配置が増減・入れ替わると指紋が変わる)。
    return impl_.AddInstance(basePath, {partTransform}, {}, ContinuityKeyOfMatrix(partTransform.world));
}

void InstanceBatchManager::ClearInstances() {
    impl_.ClearInstances();
}

void InstanceBatchManager::ClearInstances(const std::unordered_set<std::string>& keys) {
    impl_.ClearInstances(keys);
}

void InstanceBatchManager::Prune(const std::unordered_set<std::string>& liveKeys) {
    impl_.PruneUnusedCapped(liveKeys, RenderTuning::Embedded().staticInstanceBatchResidencyCap);
    for (std::unordered_set<std::string>::iterator it = failedBatchKeys_.begin();
         it != failedBatchKeys_.end();) {
        if (!liveKeys.contains(*it)) {
            it = failedBatchKeys_.erase(it);
        } else {
            ++it;
        }
    }
}

void InstanceBatchManager::Sync(UINT frameIndex) {
    impl_.Sync(frameIndex);
}

void InstanceBatchManager::RecordOpaque(ID3D12GraphicsCommandList* cmdList,
                                        const MeshConstants& base, UINT frameIndex) {
    impl_.RecordOpaque(cmdList, base, frameIndex);
}

void InstanceBatchManager::RecordBlended(ID3D12GraphicsCommandList* cmdList,
                                         const MeshConstants& base, UINT frameIndex) {
    impl_.RecordBlended(cmdList, base, frameIndex);
}

void InstanceBatchManager::RecordLateOpaque(ID3D12GraphicsCommandList* cmdList,
                                            const MeshConstants& base, UINT frameIndex) {
    impl_.RecordLateOpaque(cmdList, base, frameIndex);
}

void InstanceBatchManager::RecordTransparent(ID3D12GraphicsCommandList* cmdList,
                                             const MeshConstants& base, UINT frameIndex) {
    impl_.RecordTransparent(cmdList, base, frameIndex);
}

void InstanceBatchManager::RecordShadow(const rs::rhi::CommandList& commandList,
                                        const MeshConstants& lightConstants, UINT frameIndex,
                                        UINT shadowSlice, const rs::core::Frustum* lightFrustum) {
    impl_.RecordShadow(commandList, lightConstants, frameIndex, shadowSlice, lightFrustum);
}

void InstanceBatchManager::RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base,
                                         UINT frameIndex) {
    impl_.RecordPrepass(commandList, base, frameIndex);
}

uint64_t InstanceBatchManager::TotalInstanceCount() const {
    return impl_.TotalInstanceCount();
}

} // namespace rs::render
