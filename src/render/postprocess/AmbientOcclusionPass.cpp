#include "AmbientOcclusionPass.h"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../resource/ShaderCompileFxc.h"
#include "AmbientOcclusionShader.h"
#include "PostProcessFormats.h"
#include "SceneHdrTarget.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート署名: [0]=深度のSRV(t0)、[1]=ぼかしが読む AO の生の結果のSRV(t1)、[2]=定数 16 DWORD(b0)。
constexpr UINT kRootParamDepth = 0;
constexpr UINT kRootParamRaw = 1;
constexpr UINT kRootParamConstants = 2;

// シェーダの cbuffer AoConstants と並びを一致させる。
struct AoShaderConstants {
    float aoInvSize[2];
    float depthSize[2];
    float tanHalf[2];
    float depthLin[2];
    float radius;
    float falloffMul;
    float falloffAdd;
    float finalPower;
    float projScale;
    std::uint32_t sliceCount;
    std::uint32_t stepCount;
    std::uint32_t scale;
};
constexpr UINT kConstantDwordCount = sizeof(AoShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(AoShaderConstants) == 16 * sizeof(std::uint32_t), "AoConstants は 16 DWORD");

constexpr float kMinFalloffRange = 0.01f;
// AO の出力(見える割合 0〜1)の形式。SceneHdrTarget の HdrTargetFormat::Unorm8 と同じ。
constexpr DXGI_FORMAT kAoFormat = DXGI_FORMAT_R8_UNORM;

} // namespace

struct AmbientOcclusionPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> psoGtao;
    ComPtr<ID3D12PipelineState> psoBlur;
    SceneHdrTarget raw;  // GTAO の結果
    SceneHdrTarget blur; // ぼかし後の結果(メッシュが読む)
    std::uint32_t firstSrvIndex = 0;
    AoQuality quality = AoQuality::None;
    std::uint32_t width = 0; // 画面の大きさ(深度と同じ)
    std::uint32_t height = 0;
    std::uint32_t aoWidth = 0; // AO の大きさ(半解像度なら画面の半分)
    std::uint32_t aoHeight = 0;

    bool ResourcesReady() const { return raw.Ready() && blur.Ready(); }
};

AmbientOcclusionPass::AmbientOcclusionPass() = default;
AmbientOcclusionPass::~AmbientOcclusionPass() = default;

bool AmbientOcclusionPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                                std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->firstSrvIndex = firstSrvIndex;

    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    for (UINT i = 0; i < 2; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
    }
    D3D12_ROOT_PARAMETER rootParams[3] = {};
    for (UINT i = 0; i < 2; ++i) {
        rootParams[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[i].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[i].DescriptorTable.pDescriptorRanges = &ranges[i];
        rootParams[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    rootParams[kRootParamConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootParamConstants].Constants.ShaderRegister = 0;
    rootParams[kRootParamConstants].Constants.Num32BitValues = kConstantDwordCount;
    rootParams[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = rootParams;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[ao] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const size_t shaderSize = std::strlen(kAmbientOcclusionShader);
    ComPtr<ID3DBlob> vs = CompileFxcCached(kAmbientOcclusionShader, shaderSize, "ao.hlsl", nullptr, "VSMain",
                                           "vs_5_0", flags, "ao");
    ComPtr<ID3DBlob> psGtao = CompileFxcCached(kAmbientOcclusionShader, shaderSize, "ao.hlsl", nullptr, "PSGtao",
                                               "ps_5_0", flags, "ao");
    ComPtr<ID3DBlob> psBlur = CompileFxcCached(kAmbientOcclusionShader, shaderSize, "ao.hlsl", nullptr, "PSBlur",
                                               "ps_5_0", flags, "ao");
    if (!vs || !psGtao || !psBlur) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = impl->rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kAoFormat;
    pd.SampleDesc.Count = 1;
    pd.PS = {psGtao->GetBufferPointer(), psGtao->GetBufferSize()};
    if (FAILED(native->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&impl->psoGtao)))) {
        spdlog::error("[ao] GTAO PSO");
        return false;
    }
    pd.PS = {psBlur->GetBufferPointer(), psBlur->GetBufferSize()};
    if (FAILED(native->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&impl->psoBlur)))) {
        spdlog::error("[ao] blur PSO");
        return false;
    }

    const float clearWhite[3] = {1.0f, 1.0f, 1.0f};
    if (!impl->raw.Init(device, srvHeap, firstSrvIndex + 1, clearWhite, HdrTargetFormat::Unorm8) ||
        !impl->blur.Init(device, srvHeap, firstSrvIndex + 2, clearWhite, HdrTargetFormat::Unorm8))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool AmbientOcclusionPass::Resize(std::uint32_t width, std::uint32_t height, AoQuality quality) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.quality = quality;
    if (quality == AoQuality::None) return true;
    const bool half = AoSpecOf(quality).halfResolution;
    const std::uint32_t aoWidth = half ? (width + 1) / 2 : width;
    const std::uint32_t aoHeight = half ? (height + 1) / 2 : height;
    if (impl.ResourcesReady() && impl.width == width && impl.height == height && impl.aoWidth == aoWidth &&
        impl.aoHeight == aoHeight)
        return true;
    impl.width = 0;
    impl.height = 0;
    if (!impl.raw.Resize(aoWidth, aoHeight) || !impl.blur.Resize(aoWidth, aoHeight)) {
        spdlog::error("[ao] 資源を作れませんでした {}x{}", width, height);
        return false;
    }
    impl.width = width;
    impl.height = height;
    impl.aoWidth = aoWidth;
    impl.aoHeight = aoHeight;
    return true;
}

bool AmbientOcclusionPass::Ready(std::uint32_t width, std::uint32_t height) const {
    return impl_ && impl_->quality != AoQuality::None && impl_->ResourcesReady() && impl_->width == width &&
           impl_->height == height;
}

std::uint32_t AmbientOcclusionPass::ResultSrvIndex() const { return impl_ ? impl_->firstSrvIndex + 2 : 0; }

void AmbientOcclusionPass::Record(const rs::rhi::CommandList& commandList, PrepassDepthTarget& depth,
                                  const AoFrameParams& params) {
    if (!impl_ || !commandList.Valid() || !impl_->ResourcesReady() || !depth.Ready()) return;
    Impl& impl = *impl_;
    ID3D12GraphicsCommandList* list = commandList.Native();
    const AoPresetSpec spec = AoSpecOf(impl.quality);

    depth.EndWrite(commandList);

    AoShaderConstants constants = {};
    constants.aoInvSize[0] = 1.0f / static_cast<float>(impl.aoWidth);
    constants.aoInvSize[1] = 1.0f / static_cast<float>(impl.aoHeight);
    constants.depthSize[0] = static_cast<float>(impl.width);
    constants.depthSize[1] = static_cast<float>(impl.height);
    constants.tanHalf[0] = params.tanHalfX;
    constants.tanHalf[1] = params.tanHalfY;
    // 視空間の z = B / (d - A)。Reversed-Z(近=1、遠=0)の射影行列(PerspectiveLH)の逆。
    constants.depthLin[0] = params.nearZ / (params.nearZ - params.farZ);
    constants.depthLin[1] = params.nearZ * params.farZ / (params.farZ - params.nearZ);
    constants.radius = params.radius;
    const float falloffRange = std::max(params.falloffRange, kMinFalloffRange) * params.radius;
    const float falloffFrom = params.radius - falloffRange;
    constants.falloffMul = -1.0f / falloffRange;
    constants.falloffAdd = falloffFrom / falloffRange + 1.0f;
    constants.finalPower = params.finalPower;
    constants.projScale = static_cast<float>(impl.height) / (2.0f * params.tanHalfY);
    constants.sliceCount = spec.sliceCount;
    constants.stepCount = spec.stepCount;
    constants.scale = spec.halfResolution ? 2u : 1u;

    ID3D12DescriptorHeap* heaps[] = {impl.raw.SrvHeapRef().Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl.rootSig.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(impl.aoWidth), static_cast<float>(impl.aoHeight),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(impl.aoWidth), static_cast<LONG>(impl.aoHeight)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &constants, 0);
    const D3D12_GPU_DESCRIPTOR_HANDLE depthTable = {depth.SrvGpuHandle()};

    // GTAO。ぼかし用の t1 は、この時点で読み取りの状態にある深度を指しておく(読まれない)。
    impl.raw.BeginScene(commandList, {});
    list->SetPipelineState(impl.psoGtao.Get());
    list->SetGraphicsRootDescriptorTable(kRootParamDepth, depthTable);
    list->SetGraphicsRootDescriptorTable(kRootParamRaw, depthTable);
    list->DrawInstanced(3, 1, 0, 0);
    impl.raw.Transition(commandList, HdrTargetState::ShaderResource);

    // ぼかし。
    impl.blur.BeginScene(commandList, {});
    list->SetPipelineState(impl.psoBlur.Get());
    const D3D12_GPU_DESCRIPTOR_HANDLE rawTable = {impl.raw.SrvGpuHandle()};
    list->SetGraphicsRootDescriptorTable(kRootParamRaw, rawTable);
    list->DrawInstanced(3, 1, 0, 0);
    impl.blur.Transition(commandList, HdrTargetState::ShaderResource);
}

} // namespace rs::render
