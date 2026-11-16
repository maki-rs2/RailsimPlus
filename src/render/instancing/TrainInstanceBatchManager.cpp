#include "TrainInstanceBatchManager.h"

#include <spdlog/spdlog.h>

#include "BatchResidencyEviction.h"

namespace rs::render {
namespace {
constexpr UINT kBatchExtraSrvs = 10;
}

void TrainInstanceBatchManager::Retire(std::unordered_map<std::string, Entry>::iterator it) {
    it->second.batch->KeepGeometryForReuse();
    const SrvRangeAllocator::Range range = it->second.range;
    if (rs::rhi::RetirementQueue* queue = shared_->Retirements()) {
        queue->Retire(std::shared_ptr<TrainInstanceBatch>(std::move(it->second.batch)),
                      [allocator = allocator_, range]() { allocator->Free(range); });
    } else {
        it->second.batch.reset();
        allocator_->Free(range);
    }
}

bool TrainInstanceBatchManager::Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
                                       ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT firstSrvIndex,
                                       UINT srvPoolCapacity, UINT batchMaxTextures, UINT batchInitialInstances) {
    device_ = device; shared_ = &shared; srvHeap_ = srvHeap; srvSize_ = srvSize;
    batchMaxTextures_ = batchMaxTextures; batchInitialInstances_ = batchInitialInstances;
    allocator_ = std::make_shared<SrvRangeAllocator>();
    allocator_->Init(firstSrvIndex, srvPoolCapacity); batches_.clear(); return true;
}

bool TrainInstanceBatchManager::EnsureBatch(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                              const std::string& contentKey, const Mesh& mesh,
                                              UINT partsPerInstance,
                                              const std::vector<uint32_t>& dynamicMaterialIndices) {
    if (batches_.contains(contentKey)) return true;
    const SrvRangeAllocator::Range range = allocator_->Allocate(batchMaxTextures_ + kBatchExtraSrvs);
    if (!range.ok) return false;
    std::unique_ptr<TrainInstanceBatch> batch = std::make_unique<TrainInstanceBatch>();
    const UINT firstTransBuf = range.baseIndex + batchMaxTextures_;
    const UINT firstPartWorld = firstTransBuf + 4;
    if (!batch->Init(device, *shared_, srvHeap_, srvSize_, range.baseIndex, batchMaxTextures_, firstTransBuf, firstPartWorld, batchInitialInstances_) ||
        !batch->UploadModel(device, upload, contentKey, mesh, partsPerInstance)) { allocator_->Free(range); return false; }
    batch->ConfigureDynamicMaterialSlots(dynamicMaterialIndices);
    batches_.emplace(contentKey, Entry{std::move(batch), range}); return true;
}

bool TrainInstanceBatchManager::AddInstance(
    const std::string& contentKey, const std::vector<PartXform>& partWorlds,
    std::span<const InstanceMatOverride> materialOverrides, uint64_t instanceKey) {
    std::unordered_map<std::string, Entry>::iterator it = batches_.find(contentKey);
    if (it == batches_.end()) return false;
    return it->second.batch->AddInstance(partWorlds, materialOverrides, instanceKey);
}
bool TrainInstanceBatchManager::ApplyPreloadedTextureOverride(
    const std::string& contentKey, const std::string& basePath,
    const std::string& normalPath, const std::string& pbrPath,
    InstanceMatOverride& materialOverride) const {
    const std::unordered_map<std::string, Entry>::const_iterator it = batches_.find(contentKey);
    return it != batches_.end() && it->second.batch->ApplyPreloadedTextureOverride(
                                       basePath, normalPath, pbrPath, materialOverride);
}
bool TrainInstanceBatchManager::PreloadTextureOverride(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const std::string& contentKey, const std::string& basePath,
    const std::string& normalPath, const std::string& pbrPath,
    InstanceMatOverride& materialOverride) {
    const std::unordered_map<std::string, Entry>::iterator it = batches_.find(contentKey);
    return it != batches_.end() && it->second.batch->PreloadTextureOverride(
        device, upload, basePath, normalPath, pbrPath, materialOverride);
}
bool TrainInstanceBatchManager::TextureUnavailable(
    const std::string& contentKey, const std::string& basePath,
    const std::string& normalPath, const std::string& pbrPath) const {
    const std::unordered_map<std::string, Entry>::const_iterator it = batches_.find(contentKey);
    return it == batches_.end() || it->second.batch->TextureUnavailable(basePath, normalPath, pbrPath);
}
const std::vector<MatData>* TrainInstanceBatchManager::MaterialData(const std::string& contentKey) const {
    const std::unordered_map<std::string, Entry>::const_iterator it = batches_.find(contentKey);
    return it == batches_.end() ? nullptr : &it->second.batch->MaterialData();
}
void TrainInstanceBatchManager::ClearInstances() { for (auto& [key, entry] : batches_) entry.batch->ClearInstances(); }

void TrainInstanceBatchManager::ClearInstances(const std::unordered_set<std::string>& keys) {
    for (const std::string& key : keys) {
        std::unordered_map<std::string, Entry>::iterator it = batches_.find(key);
        if (it != batches_.end()) it->second.batch->ClearInstances();
    }
}
size_t TrainInstanceBatchManager::PruneUnused(const std::unordered_set<std::string>& liveKeys,
                                                UINT graceFrames) {
    size_t erasedCount = 0;
    for (auto it = batches_.begin(); it != batches_.end();) {
        if (liveKeys.find(it->first) != liveKeys.end()) {
            it->second.unseenFrames = 0;
            ++it;
            continue;
        }
        ++it->second.unseenFrames;
        if (it->second.unseenFrames > graceFrames) {
            Retire(it);
            it = batches_.erase(it);
            ++erasedCount;
        } else {
            ++it;
        }
    }
    return erasedCount;
}
size_t TrainInstanceBatchManager::PruneUnusedCapped(
    const std::unordered_set<std::string>& liveKeys, size_t maxResidentBatches) {
    ++liveTickCounter_;
    std::unordered_map<std::string, uint64_t> lastLiveTickByKey;
    lastLiveTickByKey.reserve(batches_.size());
    for (auto& [key, entry] : batches_) {
        if (liveKeys.find(key) != liveKeys.end()) entry.lastLiveTick = liveTickCounter_;
        lastLiveTickByKey.emplace(key, entry.lastLiveTick);
    }
    // 破棄すべきキーの選定はGPU非依存の純粋ロジック(BatchResidencyEviction)へ委譲する
    // (Catch2で直接検証できるようにするための分離)。
    const std::vector<std::string> evictKeys =
        BatchResidencyEviction::SelectEvictions(lastLiveTickByKey, liveKeys, maxResidentBatches);
    for (const std::string& key : evictKeys) {
        std::unordered_map<std::string, Entry>::iterator it = batches_.find(key);
        if (it == batches_.end()) continue;
        Retire(it);
        batches_.erase(it);
    }
    return evictKeys.size();
}
void TrainInstanceBatchManager::Sync(UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->Sync(frameIndex); }
void TrainInstanceBatchManager::RecordOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordOpaque(cmdList, base, frameIndex); }
void TrainInstanceBatchManager::RecordShadow(const rs::rhi::CommandList& commandList, const MeshConstants& lightConstants, UINT frameIndex, UINT shadowSlice, const rs::core::Frustum* lightFrustum) { for (auto& [key, entry] : batches_) entry.batch->RecordShadow(commandList, lightConstants, frameIndex, shadowSlice, lightFrustum); }
void TrainInstanceBatchManager::RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordPrepass(commandList, base, frameIndex); }
void TrainInstanceBatchManager::RecordVelocity(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordVelocity(commandList, base, frameIndex); }
void TrainInstanceBatchManager::RecordBlended(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordBlended(cmdList, base, frameIndex); }
void TrainInstanceBatchManager::RecordLateOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordLateOpaque(cmdList, base, frameIndex); }
void TrainInstanceBatchManager::RecordTransparent(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex) { for (auto& [key, entry] : batches_) entry.batch->RecordTransparent(cmdList, base, frameIndex); }
uint64_t TrainInstanceBatchManager::TotalInstanceCount() const {
    uint64_t total = 0;
    for (const auto& [key, entry] : batches_) total += uint64_t(entry.batch->InstanceCount());
    return total;
}
} // namespace rs::render
