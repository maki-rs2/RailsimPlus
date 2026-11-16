#pragma once

#include <d3d12.h>
#include <ResourceUploadBatch.h>
#include <span>
#include <string>
#include <vector>

#include "../../core/Frustum.h"
#include "../mesh/Mesh.h"
#include "../rhi/CommandList.h"
#include "../pipeline/MeshPipeline.h"

namespace rs::render {

class TrainInstanceBatch {
public:
    // このバッチを捨てた後に同じモデルへ戻るときのために、共有ジオメトリを残す。
    void KeepGeometryForReuse() const { pipeline_.KeepGeometryForReuse(); }

    bool Init(ID3D12Device* device, const MeshPipelineSharedState& shared, ID3D12DescriptorHeap* srvHeap,
              UINT srvSize, UINT firstTexIndex, UINT maxTextures, UINT firstTransBufIndex,
              UINT firstPartWorldIndex, UINT initialInstances);
    bool UploadModel(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                     const std::string& contentKey, const Mesh& mesh, UINT partsPerInstance);
    void ConfigureDynamicMaterialSlots(const std::vector<uint32_t>& dynamicMaterialIndices);
    // instanceKey は、このインスタンスが何かを表すキー(同じ車両なら毎フレーム同じ値)。
    // 並び全体の指紋が前フレームと同じ間だけ、前フレームの行列を速度の計算に使う。
    bool AddInstance(const std::vector<PartXform>& partWorlds,
                     std::span<const InstanceMatOverride> materialOverrides, uint64_t instanceKey);
    void ClearInstances();
    void Sync(UINT frameIndex);
    void RecordOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordBlended(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordLateOpaque(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    void RecordTransparent(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base, UINT frameIndex);
    // 太陽光の影パスへ不透明+カットアウトのsubmeshを深度だけで描く(半透明は影を落とさない)。
    // lightFrustum を渡すと、どのインスタンスも入らないバッチは描かない。
    void RecordShadow(const rs::rhi::CommandList& commandList, const MeshConstants& lightConstants,
                      UINT frameIndex, UINT shadowSlice,
                      const rs::core::Frustum* lightFrustum = nullptr);
    // 主視点の深度プリパスへ、深度を書く不透明系のsubmeshを深度だけで描く(base は本編と同じ視点の定数)。
    void RecordPrepass(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex);
    // 速度パス: 動いているパーツの速度を書く(前フレーム分の行列が無いバッチは何も描かない)。base は本編と同じ視点の定数。
    void RecordVelocity(const rs::rhi::CommandList& commandList, const MeshConstants& base, UINT frameIndex);
    // いずれかのインスタンス(のパーツ)の外接球が frustum と交差するか。
    bool AnyInstanceInFrustum(const rs::core::Frustum& frustum) const;
    size_t InstanceCount() const { return instanceCount_; }
    UINT PartsPerInstance() const { return partsPerInstance_; }
    const std::vector<MatData>& MaterialData() const { return pipeline_.MaterialData(); }
    bool ApplyPreloadedTextureOverride(const std::string& basePath, const std::string& normalPath,
                                       const std::string& pbrPath,
                                       InstanceMatOverride& materialOverride) const;
    bool PreloadTextureOverride(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                const std::string& basePath, const std::string& normalPath,
                                const std::string& pbrPath,
                                InstanceMatOverride& materialOverride);
    bool TextureUnavailable(const std::string& basePath, const std::string& normalPath,
                            const std::string& pbrPath) const;

private:
    static constexpr UINT kFrames = 2;
    MeshPipeline pipeline_;
    ID3D12Device* device_ = nullptr;
    UINT partsPerInstance_ = 0;
    UINT dynamicMaterialSlotCount_ = 0;
    float boundRadius_ = 0.0f; // モデル原点からの最遠頂点までの距離(外接球の半径)
    size_t instanceCount_ = 0;
    uint64_t instanceFingerprint_ = kContinuitySeed; // AddInstance したキーの並びの指紋
    std::vector<PartXform> instances_;
    std::vector<InstanceMatOverride> materialOverrides_;
    bool partWorldDirty_[kFrames] = {true, true};
    bool partWorldReady_[kFrames] = {false, false};
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> keepAlive_;
};

} // namespace rs::render
