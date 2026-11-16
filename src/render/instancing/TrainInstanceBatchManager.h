#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "TrainInstanceBatch.h"
#include "../resource/SrvRangeAllocator.h"

namespace rs::render {

class TrainInstanceBatchManager {
public:
    bool Init(ID3D12Device* device, const MeshPipelineSharedState& shared, ID3D12DescriptorHeap* srvHeap,
              UINT srvSize, UINT firstSrvIndex, UINT srvPoolCapacity, UINT batchMaxTextures,
              UINT batchInitialInstances);
    bool EnsureBatch(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                     const std::string& contentKey, const Mesh& mesh, UINT partsPerInstance,
                     const std::vector<uint32_t>& dynamicMaterialIndices);
    bool AddInstance(const std::string& contentKey, const std::vector<PartXform>& partWorlds,
                     std::span<const InstanceMatOverride> materialOverrides, uint64_t instanceKey);
    bool ApplyPreloadedTextureOverride(const std::string& contentKey, const std::string& basePath,
                                       const std::string& normalPath, const std::string& pbrPath,
                                       InstanceMatOverride& materialOverride) const;
    bool PreloadTextureOverride(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                const std::string& contentKey, const std::string& basePath,
                                const std::string& normalPath, const std::string& pbrPath,
                                InstanceMatOverride& materialOverride);
    bool TextureUnavailable(const std::string& contentKey, const std::string& basePath,
                            const std::string& normalPath, const std::string& pbrPath) const;
    void ClearInstances();
    // keys に含まれるバッチの配置だけを空にする。存在しないキーは無視する。
    void ClearInstances(const std::unordered_set<std::string>& keys);
    // liveKeys に含まれないバッチの unseenFrames を進め、graceFrames を超えたものだけを
    // 実際に破棄する(SRVレンジ返却込み)。liveKeys に含まれるバッチは unseenFrames を
    // 0へリセットする。戻り値は実際に破棄した件数(0なら何もしていない)。
    // graceFrames は _CAMDIST の量子化ヒステリシス由来のキー往復を
    // 吸収するために大きめの値を渡すこと(呼び出し側のkBatchPruneGraceFrames参照)。
    size_t PruneUnused(const std::unordered_set<std::string>& liveKeys, UINT graceFrames);
    // liveKeys に含まれないバッチを即座に破棄せず park する(GPUリソース・SRVレンジは
    // 保持したまま、ClearInstances()済みの前提で配置数0のまま次のRender()から自然に
    // スキップされる)。liveKeys に含まれるバッチは「最後にliveだった時刻」を内部の
    // 単調カウンタで更新する。呼び出し後、常駐バッチ数がmaxResidentBatchesを超えて
    // いれば、liveKeysに含まれないバッチの中から最後にliveだった時刻が古いものから
    // 順に実破棄(SRVレンジ返却込み)し、上限以下になるまで続ける。liveKeysに含まれる
    // バッチは上限超過時も破棄しない(Scene切替で表示中の静的シーナリーを常駐
    // させ続けるための猶予、graceFrames方式のPruneUnusedとは時間スケールが異なるため
    // 別メソッドにしている)。
    // 戻り値は実際に破棄した件数。
    size_t PruneUnusedCapped(const std::unordered_set<std::string>& liveKeys,
                              size_t maxResidentBatches);
    void Sync(UINT frameIndex);
    void RecordOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordBlended(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordLateOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordTransparent(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    // 全バッチを太陽光の影パスへ描く。lightFrustum を渡すと、どのインスタンスも入らないバッチは飛ばす。
    void RecordShadow(const rs::rhi::CommandList& commandList, const MeshConstants& lightConstants,
                      UINT frameIndex, UINT shadowSlice,
                      const rs::core::Frustum* lightFrustum = nullptr);
    // 全バッチを主視点の深度プリパスへ描く。
    void RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex);
    void RecordVelocity(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex);
    size_t BatchCount() const { return batches_.size(); }
    uint64_t TotalInstanceCount() const;
    bool HasBatch(const std::string& contentKey) const { return batches_.contains(contentKey); }
    const std::vector<MatData>* MaterialData(const std::string& contentKey) const;

private:
    struct Entry {
        std::unique_ptr<TrainInstanceBatch> batch;
        SrvRangeAllocator::Range range;
        // このバッチのキーが liveKeys に含まれなかった連続フレーム数。PruneUnused()が
        // 更新する。kBatchPruneGraceFrames を超えたら実際に破棄する(LOD境界で
        // ResolvedGeometrySignature()が数フレーム往復しても即破棄しないための猶予)。
        UINT unseenFrames = 0;
        // PruneUnusedCapped()専用: このバッチのキーが最後にliveKeysへ含まれていた時点の
        // liveTickCounter_の値。上限超過時の破棄候補選定(最も古いものから破棄)に使う。
        uint64_t lastLiveTick = 0;
    };
    ID3D12Device* device_ = nullptr;
    const MeshPipelineSharedState* shared_ = nullptr;
    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT srvSize_ = 0;
    UINT batchMaxTextures_ = 0;
    UINT batchInitialInstances_ = 0;
    // 解放待ちのバッチは管理元より長く生きる場合があるので、返却先も共有する。
    std::shared_ptr<SrvRangeAllocator> allocator_ = std::make_shared<SrvRangeAllocator>();
    std::unordered_map<std::string, Entry> batches_;
    // PruneUnusedCapped()の呼び出しごとに+1する単調増加カウンタ(「時刻」代わり)。
    uint64_t liveTickCounter_ = 0;
    void Retire(std::unordered_map<std::string, Entry>::iterator it);
};

} // namespace rs::render
