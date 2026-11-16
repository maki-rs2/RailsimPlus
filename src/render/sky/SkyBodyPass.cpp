#include "SkyBodyPass.h"

#include "../../core/Epsilon.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>
#include <exception>
#include <future>
#include <string>

#include <ResourceUploadBatch.h>
#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "AtmosphereGpuUtil.h"
#include "SkyBodyMath.h"
#include "SkyBodyShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;
struct SkyBodyShaderConstants {
    float mvp[16];
    float color[4];
};
constexpr UINT kConstantDwordCount = sizeof(SkyBodyShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(SkyBodyShaderConstants) == 80 && offsetof(SkyBodyShaderConstants, color) == 64,
              "SkyBodyConstants の行列・色と配置を一致させる");
constexpr UINT kVertexStride = kSkyBodyVertexFloats * sizeof(float);
constexpr std::uint32_t kSunSlot = 0;
constexpr std::uint32_t kMoonSlot = 1;
constexpr std::uint32_t kLandscapeSlot = 2;

// CPU書き込みのアップロードヒープのバッファを作ってデータを写す(頂点・インデックス。小さく、作り直さない)。
bool CreateUploadBuffer(ID3D12Device* device, const void* data, UINT64 bytes, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out))))
        return false;
    void* mapped = nullptr;
    if (FAILED(out->Map(0, nullptr, &mapped))) {
        spdlog::error("[sky_body] アップロードバッファのMapに失敗しました");
        return false;
    }
    std::memcpy(mapped, data, static_cast<size_t>(bytes));
    out->Unmap(0, nullptr);
    return true;
}

// 天体1体ぶんのGPU資源。
struct BodyGpu {
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> indexBuffer;
    ComPtr<ID3D12Resource> texture;
    D3D12_VERTEX_BUFFER_VIEW vertexView = {};
    D3D12_INDEX_BUFFER_VIEW indexView = {};
    UINT indexCount = 0;
    float diffuse[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float ambient[3] = {1.0f, 1.0f, 1.0f};
    float scale = 1.0f;
    std::uint64_t srvGpuHandle = 0;
    bool Valid() const { return indexCount > 0; }
};

} // namespace

struct SkyBodyPass::Impl {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    UINT srvStride = 0;
    std::uint32_t firstSrvIndex = 0;
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;           // 天体(加算)
    ComPtr<ID3D12PipelineState> landscapePso;  // 遠景(アルファブレンド)
    BodyGpu bodies[kSrvCount];  // kSunSlot・kMoonSlot・kLandscapeSlot

    bool CreatePipeline();
    bool UploadBody(std::uint32_t slot, const SkyBodyModel& model);
    bool UploadTexture(const ImageData& image, ComPtr<ID3D12Resource>& out);
    void WriteSrv(std::uint32_t slot, BodyGpu& body);
    void DrawBody(ID3D12GraphicsCommandList* list, const BodyGpu& body, const SkyBodyFrame& frame,
                  const rs::core::Vec3& direction, const rs::core::Mat4& rotation, const float (&color)[4]) const;
};

bool SkyBodyPass::Impl::CreatePipeline() {
    rootSig = CreateAtmosphereRootSignatureTables(device, 1, kConstantDwordCount, {}, true);
    if (!rootSig) return false;
    static_assert(kRootParamSrvTable == 0 && kRootParamConstants == 1, "SRVテーブル→ルート定数の並び");
    const std::string source = kSkyBodyDrawShader;
    ComPtr<ID3DBlob> vs = CompileAtmosphereShader(source, "VSMain", "vs_5_0", false);
    ComPtr<ID3DBlob> ps = CompileAtmosphereShader(source, "PSMain", "ps_5_0", false);
    if (!vs || !ps) return false;

    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {layout, _countof(layout)};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;  // 深度の読み書きなし
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;

    // 天体: 背面を捨てる(深度なしの加算なので、球の奥側の面まで足すと満ち欠けが消える)。.x の読込が左手系へ変換済みで、
    // メッシュ描画と同じく時計回りが表。加算系ブレンド(RailSim II の devBLEND_ADD2 の推定): 色 = SrcAlpha×Src + One×Dest。
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    blend.DestBlend = D3D12_BLEND_ONE;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        spdlog::error("[sky_body] PSO");
        return false;
    }
    // 遠景: アルファブレンド(色 = SrcAlpha×Src + (1-SrcAlpha)×Dest)。カメラはドームの中心にあり、視線は面を1回しか
    // 通らないので、面の向きに依らず描けるよう両面を描く。
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&landscapePso)))) {
        spdlog::error("[sky_body] landscape PSO");
        return false;
    }
    return true;
}

bool SkyBodyPass::Impl::UploadTexture(const ImageData& image, ComPtr<ID3D12Resource>& out) {
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = image.width;
    desc.Height = image.height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
                                               nullptr, IID_PPV_ARGS(&out))))
        return false;
    try {
        DirectX::ResourceUploadBatch upload(device);
        upload.Begin();
        D3D12_SUBRESOURCE_DATA sub = {};
        sub.pData = image.pixels.data();
        sub.RowPitch = static_cast<LONG_PTR>(image.width) * 4;
        sub.SlicePitch = sub.RowPitch * static_cast<LONG_PTR>(image.height);
        upload.Upload(out.Get(), 0, &sub, 1);
        upload.Transition(out.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        std::future<void> finished = upload.End(queue);
        finished.get();
    } catch (const std::exception& e) {
        spdlog::warn("[sky_body] テクスチャのアップロードに失敗しました: {}", e.what());
        return false;
    }
    return true;
}

void SkyBodyPass::Impl::WriteSrv(std::uint32_t slot, BodyGpu& body) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    const UINT64 index = static_cast<UINT64>(firstSrvIndex) + slot;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(index * srvStride);
    device->CreateShaderResourceView(body.texture.Get(), &view, cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = srvHeap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += index * srvStride;
    body.srvGpuHandle = gpu.ptr;
}

bool SkyBodyPass::Impl::UploadBody(std::uint32_t slot, const SkyBodyModel& model) {
    BodyGpu body;
    const UINT64 vertexBytes = model.vertices.size() * sizeof(float);
    const UINT64 indexBytes = model.indices.size() * sizeof(std::uint32_t);
    if (!CreateUploadBuffer(device, model.vertices.data(), vertexBytes, body.vertexBuffer) ||
        !CreateUploadBuffer(device, model.indices.data(), indexBytes, body.indexBuffer) ||
        !UploadTexture(model.image, body.texture))
        return false;
    body.vertexView = {body.vertexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(vertexBytes), kVertexStride};
    body.indexView = {body.indexBuffer->GetGPUVirtualAddress(), static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT};
    body.indexCount = static_cast<UINT>(model.indices.size());
    std::memcpy(body.diffuse, model.diffuse, sizeof(body.diffuse));
    std::memcpy(body.ambient, model.ambient, sizeof(body.ambient));
    body.scale = model.scale;
    WriteSrv(slot, body);
    bodies[slot] = std::move(body);
    return true;
}

void SkyBodyPass::Impl::DrawBody(ID3D12GraphicsCommandList* list, const BodyGpu& body, const SkyBodyFrame& frame,
                                 const rs::core::Vec3& direction, const rs::core::Mat4& rotation,
                                 const float (&color)[4]) const {
    const rs::core::Mat4 mvp = MakeSkyBodyMvp(frame.view, frame.proj, direction, rotation, body.scale);
    SkyBodyShaderConstants constants{};
    std::memcpy(constants.mvp, mvp.m, sizeof(constants.mvp));
    std::memcpy(constants.color, color, sizeof(constants.color));
    list->SetGraphicsRootDescriptorTable(kRootParamSrvTable, D3D12_GPU_DESCRIPTOR_HANDLE{body.srvGpuHandle});
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &constants, 0);
    list->IASetVertexBuffers(0, 1, &body.vertexView);
    list->IASetIndexBuffer(&body.indexView);
    list->DrawIndexedInstanced(body.indexCount, 1, 0, 0, 0);
}

SkyBodyPass::SkyBodyPass() = default;
SkyBodyPass::~SkyBodyPass() = default;

bool SkyBodyPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                       const rs::rhi::CommandQueue& queue, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid() || !queue.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->device = device.Native();
    impl->queue = queue.Native();
    impl->srvHeap = srvHeap.Native();
    impl->srvStride = srvHeap.Stride();
    impl->firstSrvIndex = firstSrvIndex;
    if (!impl->CreatePipeline()) return false;
    impl_ = std::move(impl);
    return true;
}

void SkyBodyPass::Upload(const SkyBodyImages& images) {
    if (!impl_) return;
    const SkyBodyModel* models[kSrvCount] = {&images.sun, &images.moon, &images.landscape};
    const char* labels[kSrvCount] = {"Sun", "Moon", "Landscape"};
    for (std::uint32_t slot = 0; slot < kSrvCount; ++slot) {
        impl_->bodies[slot] = BodyGpu{};
        if (!models[slot]->ok) continue;  // 読めなかった(指定なし含む)天体は描かない
        if (!impl_->UploadBody(slot, *models[slot])) {
            impl_->bodies[slot] = BodyGpu{};
            spdlog::warn("[sky_body] {} をGPUへ上げられませんでした", labels[slot]);
        }
    }
}

void SkyBodyPass::Clear() {
    if (!impl_) return;
    for (BodyGpu& body : impl_->bodies) body = BodyGpu{};
}

bool SkyBodyPass::HasSun() const { return impl_ && impl_->bodies[kSunSlot].Valid(); }
bool SkyBodyPass::HasMoon() const { return impl_ && impl_->bodies[kMoonSlot].Valid(); }
bool SkyBodyPass::HasLandscape() const { return impl_ && impl_->bodies[kLandscapeSlot].Valid(); }

void SkyBodyPass::Record(const rs::rhi::CommandList& commandList, const SkyBodyFrame& frame) const {
    if (!impl_ || !commandList.Valid() || (!HasSun() && !HasMoon() && !HasLandscape())) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    ID3D12DescriptorHeap* heaps[] = {impl_->srvHeap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const bool hasSunDirection = frame.directionToSun.Length() > rs::core::kDegenerateVectorEps;
    const rs::core::Vec3 sunDirection = frame.directionToSun.Normalized();
    // RailSim II と同じ順: Sun → Moon → 遠景。
    if (HasSun() && hasSunDirection) {
        const rs::core::Vec3 worldUp(0.0f, 1.0f, 0.0f);
        impl_->DrawBody(list, impl_->bodies[kSunSlot], frame, sunDirection,
                        MakeSkyBodyRotation(sunDirection, worldUp), impl_->bodies[kSunSlot].diffuse);
    }
    if (HasMoon() && hasSunDirection && frame.directionToMoon.Length() > rs::core::kDegenerateVectorEps) {
        // 月の前方は太陽と反対(-sdir)。上の目安は RailSim II と同じ (sdir.y, sdir.z, sdir.x)(前方と垂直な成分だけが使われる)。
        const rs::core::Vec3 moonUpHint(sunDirection.y, sunDirection.z, sunDirection.x);
        impl_->DrawBody(list, impl_->bodies[kMoonSlot], frame, frame.directionToMoon.Normalized(),
                        MakeSkyBodyRotation(sunDirection * -1.0f, moonUpHint), impl_->bodies[kMoonSlot].diffuse);
    }
    if (HasLandscape()) {
        // 方向ゼロ・回転なしでカメラ位置にドームを置く。色はアンビエント × 光の倍率(ディフューズは使わない)、
        // アルファは材質のアルファ。
        const BodyGpu& dome = impl_->bodies[kLandscapeSlot];
        const float color[4] = {dome.ambient[0] * frame.landscapeLight[0], dome.ambient[1] * frame.landscapeLight[1],
                                dome.ambient[2] * frame.landscapeLight[2], dome.diffuse[3]};
        list->SetPipelineState(impl_->landscapePso.Get());
        impl_->DrawBody(list, dome, frame, rs::core::Vec3(0.0f, 0.0f, 0.0f), rs::core::Mat4::Identity(), color);
    }
}

} // namespace rs::render
