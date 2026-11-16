//
// ThumbnailRenderer.cpp - ThumbnailRenderer.h の実装(DX12の資源はすべてImplの中)
//
#include "ThumbnailRenderer.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <ResourceUploadBatch.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <future>
#include <span>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "../mesh/Mesh.h"
#include "../pipeline/MeshFrameRecord.h"
#include "../pipeline/MeshPipeline.h"
#include "../pipeline/MeshPipelineSharedState.h"
#include "../postprocess/PostProcessFormats.h"
#include "../postprocess/SceneHdrTarget.h"
#include "../postprocess/TonemapMath.h"
#include "ThumbnailLighting.h"
#include "../postprocess/TonemapPass.h"
#include "../resource/RenderTuning.h"
#include "../resource/TextureManager.h"
#include "../sky/EnvironmentState.h"
#include "ThumbnailRendererImpl.h"

namespace rs::render {

namespace {

// 本編のメッシュ描画と同じ昼夜係数(1=昼。サムネイルは常に昼の光で撮る)。
constexpr float kThumbnailDayAlpha = 1.0f;

// バッファ側(配置つき)・テクスチャ側(サブリソース0)のコピー位置。
D3D12_TEXTURE_COPY_LOCATION PlacedLocation(ID3D12Resource* buffer, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint) {
    D3D12_TEXTURE_COPY_LOCATION location = {};
    location.pResource = buffer;
    location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    location.PlacedFootprint = footprint;
    return location;
}

D3D12_TEXTURE_COPY_LOCATION TextureLocation(ID3D12Resource* texture) {
    D3D12_TEXTURE_COPY_LOCATION location = {};
    location.pResource = texture;
    location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    location.SubresourceIndex = 0;
    return location;
}


} // namespace

void ThumbnailRenderer::Impl::RecordScene(ID3D12GraphicsCommandList* list, const ThumbnailScene& scene) {
    // 光が進む向きは、環境光(IBL)を作った太陽の反対(本編の sunDirection_ と同じ意味)。
    const rs::core::Vec3 sunToSun = ThumbnailSunDirectionToSun();
    const float lightDir[3] = {-sunToSun.x, -sunToSun.y, -sunToSun.z};
    MeshConstants constants = MakeBaseMeshConstants(
        lightDir, kThumbnailDayAlpha,
        BuildEnvironmentState(RenderTuning::Embedded(), environment.SrvIndices(), environment.Ready(), PluginSkyInput{},
                              false, SunToSunInput{sunToSun.x, sunToSun.y, sunToSun.z}));
    // 高さフォグは本編と同じ式・同じ埋め込みの値で入れる。ユーザーのスイッチには従わない(撮り済みの画像と食い違うため)。
    // APは入れない(距離が近く、LUTの最初のスライスより手前では散乱光がほぼ0)。
    ApplyHeightFogToMeshConstants(MakeHeightFogFrame(RenderTuning::Embedded(), true), constants);
    // 毎回フェンスで待ってから書くので、フレーム番号・枠は0固定でよい。
    ApplyViewToMeshConstants(constants, viewConstants.Write(0, 0, MakeViewConstants(scene.viewProj, scene.cameraPos)),
                             scene.cameraPos);

    // 毎回フェンスで待ってから書くので、フレーム番号は0固定でよい(2面あるバッファの片方しか使わない)。
    const MeshDrawEntry entry{&pipeline, 1.0f};
    MeshFrameTargets targets;
    targets.wboit = &wboit;
    targets.depthDsv = rs::rhi::CpuDescriptor{pipeline.DepthDsv().ptr};
    targets.compositeRtv = rs::rhi::CpuDescriptor{hdrTarget.Rtv().ptr};
    targets.compositeTarget = &hdrTarget;
    RecordMeshFrame(rs::rhi::CommandList(list), constants, 0, std::span<const MeshDrawEntry>(&entry, 1), targets, {});
}

void ThumbnailRenderer::Impl::RecordCommands(ID3D12GraphicsCommandList* list, const ThumbnailScene* scene) {
    const rs::rhi::CommandList commandList(list);

    // 背景: HDRターゲットへ背景のグラデーションをコピーして、描画先の状態へ移す。
    hdrTarget.RecordCopyBackground(commandList);

    const D3D12_CPU_DESCRIPTOR_HANDLE hdrRtv = {hdrTarget.Rtv().ptr};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = pipeline.DepthDsv();
    ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(config.width),
                                     static_cast<float>(config.height), 0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(config.width), static_cast<LONG>(config.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->OMSetRenderTargets(1, &hdrRtv, FALSE, &dsv);
    // reversed-Z なので、遠方でクリアする(MeshPipeline::CreateDepthBuffer のクリア値と同じ)。
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, kReversedZClearDepth, 0, 0, nullptr);
    if (scene) RecordScene(list, *scene);

    // トーンマップ: HDRターゲットを最終出力RTへ変換する(本編と同じPSO・設定)。
    // 最終出力RTは COPY_SOURCE(撮影後の待機状態)から描画先の状態へ移し、終わったら戻す。
    hdrTarget.Transition(commandList, HdrTargetState::ShaderResource);
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = renderTarget.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &barrier);
    const rs::rhi::CpuDescriptor outputRtv = {rtvHeap->GetCPUDescriptorHandleForHeapStart().ptr};
    tonemap->Record(commandList, hdrTarget, outputRtv);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);

    // 最終出力RTをリードバックへコピーする。
    const D3D12_TEXTURE_COPY_LOCATION readbackDst = PlacedLocation(readback.Get(), footprint);
    const D3D12_TEXTURE_COPY_LOCATION renderTargetSrc = TextureLocation(renderTarget.Get());
    list->CopyTextureRegion(&readbackDst, 0, 0, 0, &renderTargetSrc, nullptr);
}

ThumbnailRenderer::ThumbnailRenderer() = default;
ThumbnailRenderer::~ThumbnailRenderer() = default;

bool ThumbnailRenderer::Init(const rs::rhi::Device& device, const MeshPipelineSharedState& shared,
                             const TonemapPass& tonemap, const ThumbnailRendererConfig& config) {
    impl_.reset();
    ID3D12Device* native = device.Native();
    const std::uint64_t srvNeeded = static_cast<std::uint64_t>(config.maxTextures) +
                                    config.transBufSrvCount + config.partWorldSrvCount;
    if (!native || !tonemap.Ready() || config.width == 0 || config.height == 0 || config.maxTextures == 0 ||
        srvNeeded > config.srvStride) {
        spdlog::error("[thumbnail] 設定が不正です");
        return false;
    }
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->config = config;
    impl->device = native;
    impl->tonemap = &tonemap;
    if (!impl->CreateQueueAndSync() || !impl->CreateTargets() || !impl->CreateSrvHeapAndPipeline(shared)) {
        spdlog::error("[thumbnail] GPU資源の作成に失敗しました");
        return false;
    }
    impl->GenerateEnvironment();
    impl->usable = true;
    impl_ = std::move(impl);
    return true;
}

bool ThumbnailRenderer::Render(const ThumbnailScene& scene, ImageData& out) {
    if (!scene.mesh || scene.mesh->Empty()) {
        out = ImageData{};
        return false;
    }
    return Capture(&scene, out);
}

bool ThumbnailRenderer::RenderBackground(ImageData& out) {
    return Capture(nullptr, out);
}

bool ThumbnailRenderer::Capture(const ThumbnailScene* scene, ImageData& out) {
    out = ImageData{};
    if (!impl_ || !impl_->usable) return false;
    Impl& impl = *impl_;
    ID3D12Device* device = impl.device.Get();

    // 撮り終えたら(失敗時も)、この1枚のためのメッシュとテクスチャを手放す。
    std::vector<ComPtr<ID3D12Resource>> keepAlive;
    bool gpuSubmitted = false;
    const auto releaseMesh = [&] {
        keepAlive.clear();
        impl.pipeline.Reset();
        impl.hdrTarget.ForceState(HdrTargetState::ShaderResource); // 未実行の記録を捨てた場合の追跡の巻き戻し
        impl.textureManager.Clear();
    };

    try {
        // 1. メッシュとテクスチャのアップロード。テクスチャのコピーは専用のバッチ(自前のキュー)で
        //    行い、完了を待ってから描く。geometryKey が空なので頂点・インデックスはUPLOADヒープへ直接置く。
        //    背景だけを撮るとき(scene が null)は、メッシュが無いのでアップロードも描画記録も飛ばす。
        if (scene) {
            DirectX::ResourceUploadBatch upload(device);
            upload.Begin();
            const bool uploaded = impl.pipeline.UploadMesh(device, upload, *scene->mesh, keepAlive);
            std::future<void> uploadFinished = upload.End(impl.queue.Get());
            uploadFinished.get();
            if (!uploaded) {
                releaseMesh();
                return false;
            }

            // 2. 描画の記録。パーツ行列は単位行列1つ(メッシュ側に焼き込み済み)。
            PartXform identity = {};
            for (size_t i = 0; i < 4; ++i) identity.world[i * 5] = identity.normalMat[i * 5] = 1.0f;
            impl.pipeline.UpdatePartWorlds({identity}, 0);
        }

        if (FAILED(impl.allocator->Reset()) ||
            FAILED(impl.cmdList->Reset(impl.allocator.Get(), nullptr))) {
            releaseMesh();
            return false;
        }
        ID3D12GraphicsCommandList* list = impl.cmdList.Get();

        impl.RecordCommands(list, scene);
        if (FAILED(list->Close())) {
            releaseMesh();
            return false;
        }
        ID3D12CommandList* lists[] = {list};
        impl.queue->ExecuteCommandLists(1, lists);
        gpuSubmitted = true;
        if (!impl.WaitIdle()) {
            // 完了を確認できないGPU資源は触らず、以後の撮影をやめる(キューが止まっている可能性がある)。
            spdlog::error("[thumbnail] GPUの完了待ちに失敗しました。以後のサムネイルは撮りません");
            impl.usable = false;
            return false;
        }
        const bool ok = impl.ReadBack(out);
        releaseMesh();
        impl.CheckDeviceLost();
        if (!ok) out = ImageData{};
        return ok;
    } catch (const std::exception& e) {
        spdlog::error("[thumbnail] 撮影中に例外: {}", e.what());
        out = ImageData{};
        impl.CheckDeviceLost();
        if (gpuSubmitted) impl.usable = false; // 提出済みで完了を確認できていない
        else releaseMesh();
        return false;
    }
}

} // namespace rs::render
