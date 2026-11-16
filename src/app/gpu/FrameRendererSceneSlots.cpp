#include "FrameRenderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <spdlog/spdlog.h>

#include "imgui.h"

#include "../../platform/font/UiFont.h"
#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/resource/GeometryManager.h"
#include "../../render/sky/EnvironmentState.h"
#include "../../render/resource/RenderTuning.h"
#include "../../render/resource/XLoader.h"
#include "../../core/Math.h"
#include "App.h"
#include "CameraMath.h"
#include "GpuContext.h"

namespace rs::app {

using rs::core::Normalize3;


// FrameRenderer のシーン常駐スロット(確保・アップロード・破棄・可視/フェード)の管理。

bool FrameRenderer::EnsureSceneSlotCapacity(GpuContext& gpu, UINT slot) {
    while (slot >= scenePipelines_.size()) {
        const rs::render::SrvRangeAllocator::Range range =
            sceneAllocator_.Allocate(kSceneEntitySrvStride);
        if (!range.ok) {
            // 枯渇状態は解消されるまでスロット確保のたびに再発するため、フレーム毎の
            // 連続警告を避けて1回だけ出す(MeshPipelineRecord::warnCbOverflow等と同じ
            // 抑制パターン)。
            if (!warnedScenePoolExhaustion_) {
                spdlog::warn("[frame_renderer] scene SRV pool exhausted at slot {}",
                            scenePipelines_.size());
                warnedScenePoolExhaustion_ = true;
            }
            return false;
        }
        const UINT firstTex = range.baseIndex;
        const UINT firstTransBuf = firstTex + kSceneEntityMaxTextures;
        const UINT firstPartWorld = firstTransBuf + kTransBufSrvCount;
        scenePipelines_.emplace_back();
        if (!scenePipelines_.back().Init(gpu.Device(), meshPipelineShared_, gpu.SrvHeap(),
                                         gpu.SrvStride(), firstTex, kSceneEntityMaxTextures,
                                         firstTransBuf, firstPartWorld, firstPartWorld + kPartWorldSrvCount, kSceneEntityMaxParts,
                                         rs::render::GetTextureManager())) {
            scenePipelines_.pop_back();
            sceneAllocator_.Free(range);
            return false;
        }
        sceneMeshBounds_.emplace_back();
        scenePartWorlds_.emplace_back();
        sceneDynamicVertices_.emplace_back();
        sceneSlotState_.GrowToInclude(slot);
    }
    return true;
}

bool FrameRenderer::UploadSceneMeshToGpu(GpuContext& gpu, UINT slot,
                                         const std::string& geometryKey,
                                         const rs::render::Mesh& mesh) {
    if (!EnsureSceneSlotCapacity(gpu, slot)) return false;
    // ChangeModel はフレーム進行中に既存のscene slotを差し替え、視点の移動で頻繁に起きる。
    // GPUの完了は待たず、前フレームが使っている資源を次の順番まで残して差し替える。
    // メッシュの枠番号は frameCounter_ から決まる(スワップチェーンの番号とは別)。
    scenePipelines_[slot].BeginInFlightSwap(UINT(frameCounter_));
    bool ok = false;
    gpu.RunUploadCommands(
        [&](ID3D12Device* device, DirectX::ResourceUploadBatch& upload) {
            ok = UploadSceneMeshWithBatch(device, upload, slot, geometryKey, mesh);
        },
        UploadCompletion::NoWait);
    return ok;
}

bool FrameRenderer::UploadSceneMeshWithBatch(ID3D12Device* device,
                                             DirectX::ResourceUploadBatch& upload, UINT slot,
                                             const std::string& geometryKey,
                                             const rs::render::Mesh& mesh) {
    SceneSlotMeshBounds& meshBounds = sceneMeshBounds_[slot];
    meshBounds.hasVertices = !mesh.vertices.empty();
    std::memcpy(meshBounds.aabbMin, mesh.aabbMin, sizeof(meshBounds.aabbMin));
    std::memcpy(meshBounds.aabbMax, mesh.aabbMax, sizeof(meshBounds.aabbMax));
    sceneSlotState_.Bounds()[slot].dirty = true;
    // 古い写しは捨てる。新しいメッシュには、上げたときの頂点が入っている。
    sceneDynamicVertices_[slot] = SceneDynamicVertices{};
    sceneDynamicVertices_[slot].expectedCount = mesh.dynamicVertexCount;
    return scenePipelines_[slot].UploadMesh(device, upload, mesh, keepAlive_, geometryKey);
}

void FrameRenderer::SetScenePartWorlds(UINT slot, const std::vector<rs::render::PartXform>& xforms,
                                       uint64_t continuityKey) {
    if (slot >= scenePipelines_.size()) return;
    scenePartWorlds_[slot] = xforms;
    sceneSlotState_.PartWorldKeys()[slot] = continuityKey;
    sceneSlotState_.PartWorldDirty()[slot] = {true, true};
    sceneSlotState_.Bounds()[slot].dirty = true;
}

void FrameRenderer::SetSceneDynamicVertices(UINT slot,
                                            const std::vector<rs::render::MeshVertex>& vertices) {
    if (slot >= scenePipelines_.size()) return;
    SceneDynamicVertices& dynamic = sceneDynamicVertices_[slot];
    if (dynamic.expectedCount == 0 || vertices.size() != dynamic.expectedCount) {
        spdlog::warn("[frame_renderer] slot {} の書き換える頂点の個数が合わない: 渡された{}、期待{}",
                     slot, vertices.size(), dynamic.expectedCount);
        return;
    }
    dynamic.vertices = vertices;
    dynamic.dirty = {true, true};
    // カリングの範囲は広げるだけにする(縮めると、前フレームの枠が使う頂点が範囲外になりうる)。
    SceneSlotMeshBounds& meshBounds = sceneMeshBounds_[slot];
    for (const rs::render::MeshVertex& v : vertices) {
        for (int axis = 0; axis < 3; ++axis) {
            meshBounds.aabbMin[axis] = std::min(meshBounds.aabbMin[axis], v.pos[axis]);
            meshBounds.aabbMax[axis] = std::max(meshBounds.aabbMax[axis], v.pos[axis]);
        }
    }
    sceneSlotState_.Bounds()[slot].dirty = true;
}

void FrameRenderer::FlushScenePartWorlds(UINT frameIndex) {
    const size_t frame = static_cast<size_t>(frameIndex % 2);
    for (size_t slot = 0; slot < scenePipelines_.size(); ++slot) {
        scenePipelines_[slot].AdvanceInFlightSwap(frameIndex);
        SceneDynamicVertices& dynamic = sceneDynamicVertices_[slot];
        if (dynamic.dirty[frame]) {
            scenePipelines_[slot].WriteDynamicVertices(frameIndex, dynamic.vertices.data(),
                                                       uint32_t(dynamic.vertices.size()));
            dynamic.dirty[frame] = false;
        }
        if (!sceneSlotState_.PartWorldDirty()[slot][frame]) continue;
        scenePipelines_[slot].UpdatePartWorlds(scenePartWorlds_[slot], frameIndex,
                                               sceneSlotState_.PartWorldKeys()[slot]);
        // 動いているパーツがある間は、書いた枠に前フレームの行列(速度)が残るので、両方の枠を
        // 次のフレームも書き直す。止まった後も、もう一方の枠が1回書き直されて速度 0 になる。
        std::array<bool, 2>& dirty = sceneSlotState_.PartWorldDirty()[slot];
        if (scenePipelines_[slot].PartWorldsMoving()) {
            dirty = {true, true};
        } else {
            dirty[frame] = false;
        }
    }
}

bool FrameRenderer::HasSceneMesh(UINT slot) const {
    return slot < scenePipelines_.size() && scenePipelines_[slot].HasMesh();
}

bool FrameRenderer::UploadPlacementPreviewMesh(GpuContext& gpu, const rs::render::Mesh& mesh) {
    return placementPreviewState_.UploadMesh(gpu, mesh, keepAlive_);
}

void FrameRenderer::SetPlacementPreviewWorlds(const std::vector<rs::core::Mat4>& worlds) {
    placementPreviewState_.SetWorlds(worlds, static_cast<UINT>(frameCounter_));
}

void FrameRenderer::ClearSceneSlot(GpuContext& gpu, UINT slot) {
    ClearSceneSlotInternal(gpu, slot, true);
}

bool FrameRenderer::ClearSceneSlotGeometryOnly(GpuContext& gpu, UINT slot, UINT requiredTexSlots) {
    if (slot >= scenePipelines_.size()) return false;
    // テクスチャ枠が足りなければ保持を諦めてプールごと破棄し、枠を巻き戻す。
    // 保持したまま差し替え続けると nextTexSlot_ が単調増加し、表示パターンごとに
    // 別画像を持つ情報表示系プラグインでは枠切れ(=以後そのパスがずっと
    // 読み込めずプレースホルダ化)を起こすため(MeshPipeline.h の texBudgetFailed_ 参照)。
    const bool keepTextures = scenePipelines_[slot].RemainingTextureSlots() >= requiredTexSlots;
    ClearSceneSlotInternal(gpu, slot, !keepTextures);
    return keepTextures;
}

void FrameRenderer::ClearSceneSlotInternal(GpuContext& gpu, UINT slot, bool discardTextures) {
    if (slot >= scenePipelines_.size()) return;
    gpu.WaitForGpu();
    scenePipelines_[slot].ResetGeometry();
    // テクスチャプールの破棄は任意。同一フォルダのスイッチ切替でジオメトリだけ
    // 差し替える場合、テクスチャはほぼ不変なので保持すると再構築を丸ごと省ける
    // (テクスチャ再構築が切替時間の大半を占める。MeshPipeline::ResetGeometry のコメント参照)。
    if (discardTextures) scenePipelines_[slot].DiscardTexturePool();
    sceneMeshBounds_[slot] = SceneSlotMeshBounds{};
    scenePartWorlds_[slot].clear();
    sceneDynamicVertices_[slot] = SceneDynamicVertices{};
    sceneSlotState_.PartWorldKeys()[slot] = 0;
    sceneSlotState_.PartWorldDirty()[slot] = {true, true};
    sceneSlotState_.Bounds()[slot].dirty = true;
    sceneSlotState_.Bounds()[slot].hasAabb = false;
    // スロット再利用時に前回の非表示状態が残ると、次にこのスロットへ書かれる
    // 別エンティティが描かれなくなるため、必ずtrueへ戻す。
    sceneSlotState_.Visible()[slot] = true;
    sceneSlotState_.FadeAlpha()[slot] = 1.0f;
    // レイアウトを閉じたときなど、シーンが空になったら残しておく理由がない。
    const bool anySceneMesh = std::any_of(
        scenePipelines_.begin(), scenePipelines_.end(),
        [](const rs::render::MeshPipeline& pipeline) { return pipeline.HasMesh(); });
    if (!anySceneMesh) rs::render::GetGeometryManager().ClearRecent();
}

void FrameRenderer::SetSceneSlotVisible(UINT slot, bool visible) {
    if (slot >= sceneSlotState_.Visible().size()) return;
    sceneSlotState_.Visible()[slot] = visible;
}

void FrameRenderer::SetSceneSlotFadeAlpha(UINT slot, float alpha) {
    if (slot >= sceneSlotState_.FadeAlpha().size()) return;
    sceneSlotState_.FadeAlpha()[slot] = std::clamp(alpha, 0.0f, 1.0f);
}

rs::render::MeshPipeline& FrameRenderer::ScenePipeline(UINT slot) {
    return scenePipelines_[slot];
}

} // namespace rs::app
