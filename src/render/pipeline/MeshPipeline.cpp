// MeshPipeline.cpp - MeshPipeline の初期化(Init)・深度バッファ・パーツワールド
// 行列更新。ルートシグネチャ/PSO生成は共有オブジェクト(MeshPipelineSharedState、
// 呼び出し側が1回だけInit済みのものを渡す)、毎フレームの描画コマンド記録は
// MeshPipelineRecord.cppへ分離。
#include "MeshPipeline.h"

#include <algorithm>
#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

namespace rs::render {

bool MeshPipeline::Init(ID3D12Device* device, const MeshPipelineSharedState& shared,
                        ID3D12DescriptorHeap* srvHeap, UINT srvDescriptorSize,
                        UINT firstTextureDescriptorIndex, UINT maxTextures,
                        UINT firstTransBufDescriptorIndex,
                        UINT firstPartWorldDescriptorIndex, UINT firstInstanceMatOverrideDescriptorIndex,
                        UINT maxParts, TextureManager& textureManager) {
    shared_ = &shared;
    constantBuffer_.SetRetirementQueue(shared.Retirements());
    shadowConstantBuffer_.SetRetirementQueue(shared.Retirements());
    dynamicBuffers_.SetRetirementQueue(shared.Retirements());
    device_ = device;
    srvHeap_ = srvHeap;
    srvSize_ = srvDescriptorSize;
    texturePool_.Init(srvHeap, srvDescriptorSize, firstTextureDescriptorIndex, maxTextures,
                      textureManager);
    transparency_.Init(srvHeap, srvDescriptorSize, firstTransBufDescriptorIndex);

    // 定数バッファ (256B アライン * capacity)。実際に必要な容量(submesh数+2)は
    // 初回 UploadMesh() が EnsureConstantBufferCapacity() 経由で確定するため、
    // ここでは最小容量(1枠)だけ確保しておく(大量車両レイアウトでのVRAM過剰確保を
    // 避けるため、想定上限で固定確保しない)。
    if (!constantBuffer_.Init(device)) return false;

    // gPartWorld/InstanceMatOverride/DynamicMaterialSlot の確保・恒等行列初期化は
    // MeshDynamicBuffers::Init() 側で行う(MeshDynamicBuffers.cpp参照)。
    return dynamicBuffers_.Init(device, srvHeap, srvDescriptorSize, firstPartWorldDescriptorIndex,
                                firstInstanceMatOverrideDescriptorIndex, maxParts);
}

// --- テクスチャプール系の薄い転送(実体は MeshTexturePool.cpp)。公開シグネチャは
// 分離前と完全に維持する(呼び出し元7ファイルを変更しないため)。

bool MeshPipeline::PreloadTexture(ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
                                  const std::string& basePath, const std::string& normalPath,
                                  const std::string& pbrPath, PreloadedTexInfo& infoOut) {
    return texturePool_.PreloadTexture(device, upload, basePath, normalPath, pbrPath, infoOut);
}

bool MeshPipeline::FindPreloadedSlot(const std::string& basePath, const std::string& normalPath,
                                     const std::string& pbrPath, PreloadedTexInfo& infoOut) const {
    return texturePool_.FindPreloadedSlot(basePath, normalPath, pbrPath, infoOut);
}

void MeshPipeline::ApplyInstanceMaterialTexture(InstanceMatOverride& value, bool hasTexture,
                                                const PreloadedTexInfo& info) const {
    texturePool_.ApplyInstanceMaterialTexture(value, hasTexture, info);
}

// --- gPartWorld/InstanceMatOverride/DynamicMaterialSlot 系の薄い転送(実体は
// MeshDynamicBuffers.cpp)。公開シグネチャは分離前と完全に維持する。

bool MeshPipeline::EnsureInstanceMatOverrideCapacity(ID3D12Device* device, UINT requiredOverrides, UINT frameIndex) {
    return dynamicBuffers_.EnsureInstanceMatOverrideCapacity(device, requiredOverrides, frameIndex);
}

void MeshPipeline::UpdateInstanceMatOverrides(const std::vector<InstanceMatOverride>& overrides, UINT frameIndex) {
    dynamicBuffers_.UpdateInstanceMatOverrides(overrides, frameIndex);
}

void MeshPipeline::ConfigureDynamicMaterialSlots(const std::vector<uint32_t>& materialIndices) {
    dynamicBuffers_.ConfigureDynamicMaterialSlots(materialIndices, materialSet_.Count());
}

bool MeshPipeline::EnsurePartWorldCapacity(ID3D12Device* device, UINT requiredParts,
                                           UINT frameIndex) {
    return dynamicBuffers_.EnsurePartWorldCapacity(device, requiredParts, frameIndex);
}

void MeshPipeline::BeginInFlightSwap(UINT currentFrame) {
    geometry_.RetireBuffers(shared_->Retirements());
    transparency_.PreserveFrameForSwap((currentFrame + 1) % kFrames);
}

void MeshPipeline::AdvanceInFlightSwap(UINT frameIndex) {
    transparency_.ApplyPreservedFrame(frameIndex % kFrames);
    geometry_.SelectDynamicFrame(frameIndex % kFrames);
}

bool MeshPipeline::WriteDynamicVertices(UINT frameIndex, const MeshVertex* vertices, uint32_t count) {
    return geometry_.WriteDynamicVertices(frameIndex % kFrames, vertices, count);
}

void MeshPipeline::UpdatePartWorlds(const std::vector<PartXform>& xforms,
                                    UINT frameIndex, uint64_t continuityKey) {
    dynamicBuffers_.UpdatePartWorlds(xforms, frameIndex, continuityKey);
}

bool MeshPipeline::CreateDepthBuffer(ID3D12Device* device, UINT width, UINT height) {
    if (width == 0 || height == 0) return false;
    if (!dsvHeap_) {
        D3D12_DESCRIPTOR_HEAP_DESC dh = {};
        dh.NumDescriptors = 2;
        dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        if (FAILED(device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsvHeap_))))
            return false;
        dsvDescriptorSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    }
    depthBuffer_.Reset();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    // 資源はTYPELESSにして、DSV(D32_FLOAT)と読み取りのSRV(R32_FLOAT)の両方から見られるようにする。
    rd.Format = DXGI_FORMAT_R32_TYPELESS;
    rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = kReversedZClearDepth;

    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear,
            IID_PPV_ARGS(&depthBuffer_))))
        return false;

    D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
    dv.Format = DXGI_FORMAT_D32_FLOAT;
    dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    device->CreateDepthStencilView(depthBuffer_.Get(), &dv,
                                   dsvHeap_->GetCPUDescriptorHandleForHeapStart());
    dv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    D3D12_CPU_DESCRIPTOR_HANDLE readOnly = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
    readOnly.ptr += dsvDescriptorSize_;
    device->CreateDepthStencilView(depthBuffer_.Get(), &dv, readOnly);
    return true;
}

bool MeshPipeline::WriteDepthSrv(const rs::rhi::SrvHeap& srvHeap, std::uint32_t srvIndex) const {
    if (!depthBuffer_ || !srvHeap.Valid()) return false;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (FAILED(depthBuffer_->GetDevice(IID_PPV_ARGS(&device)))) return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_R32_FLOAT;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap.Native()->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srvIndex) * srvHeap.Stride();
    device->CreateShaderResourceView(depthBuffer_.Get(), &view, cpu);
    return true;
}

bool MeshPipeline::RecordDepthProbe(const rs::rhi::CommandList& commandList, unsigned x, unsigned y) {
    return depthProbe_.Record(commandList, depthBuffer_.Get(), x, y);
}

void MeshPipeline::TransitionDepth(const rs::rhi::CommandList& commandList, bool toShaderRead) const {
    if (!depthBuffer_ || !commandList.Valid()) return;
    constexpr D3D12_RESOURCE_STATES kReadStates = D3D12_RESOURCE_STATE_DEPTH_READ |
                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = depthBuffer_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = toShaderRead ? D3D12_RESOURCE_STATE_DEPTH_WRITE : kReadStates;
    barrier.Transition.StateAfter = toShaderRead ? kReadStates : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    commandList.Native()->ResourceBarrier(1, &barrier);
}

} // namespace rs::render
