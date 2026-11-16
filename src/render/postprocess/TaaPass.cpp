#include "TaaPass.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstring>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "../resource/DxcShaderCompiler.h"
#include "PostProcessFormats.h"
#include "TaaShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート定数(HLSL の TaaConstants と並びを一致させる)。
struct TaaShaderConstants {
    float invViewProj[16];
    float prevViewProj[16];
    float jitter[2];
    float invSize[2];
    float size[2];
    float historyWeight;
    float sharpness;
    std::uint32_t colorIdx;
    std::uint32_t historyIdx;
    std::uint32_t depthIdx;
    std::uint32_t velocityIdx;
    std::uint32_t reset;
    std::uint32_t pad[3];
};
constexpr UINT kConstantDwordCount = sizeof(TaaShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(TaaShaderConstants) == 192, "TaaConstants は 48 DWORD(HLSL の cbuffer と同じ並び)");

} // namespace

struct TaaPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
    SceneHdrTarget history;
    SceneHdrTarget resolved;
    rs::rhi::SrvHeap srvHeap;
    std::uint32_t firstSrvIndex = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

TaaPass::TaaPass() = default;
TaaPass::~TaaPass() = default;

bool TaaPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    impl->srvHeap = srvHeap;
    impl->firstSrvIndex = firstSrvIndex;

    // ルート署名: 32bit 定数 b0 と、履歴を線形で引く静的サンプラ s0。SRV は ResourceDescriptorHeap[] から番号で引く。
    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = kConstantDwordCount;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[taa] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    DxcShaderCompiler dxc;
    ComPtr<IDxcBlob> vs, ps;
    if (!dxc.Init() || !dxc.Compile(kTaaShader, L"VSMain", L"vs_6_6", {}, "taa", vs) ||
        !dxc.Compile(kTaaShader, L"PSResolve", L"ps_6_6", {}, "taa", ps))
        return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = impl->rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;
    if (FAILED(native->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&impl->pso)))) {
        spdlog::error("[taa] PSO");
        return false;
    }

    const float black[3] = {0.0f, 0.0f, 0.0f};
    if (!impl->history.Init(device, srvHeap, firstSrvIndex, black) ||
        !impl->resolved.Init(device, srvHeap, firstSrvIndex + 1, black))
        return false;
    impl_ = std::move(impl);
    return true;
}

bool TaaPass::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.width = 0;
    impl.height = 0;
    if (!impl.history.Resize(width, height) || !impl.resolved.Resize(width, height)) {
        spdlog::error("[taa] 資源を作れませんでした {}x{}", width, height);
        return false;
    }
    impl.width = width;
    impl.height = height;
    return true;
}

bool TaaPass::Ready(std::uint32_t width, std::uint32_t height) const {
    return impl_ && impl_->width == width && impl_->height == height && impl_->history.Ready() &&
           impl_->resolved.Ready();
}

std::uint32_t TaaPass::DepthSrvIndex() const { return impl_ ? impl_->firstSrvIndex + 2 : 0; }

SceneHdrTarget& TaaPass::Resolved() { return impl_->resolved; }

void TaaPass::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& current,
                     const TaaFrameParams& params) {
    if (!impl_ || !commandList.Valid() || !current.Ready() || !impl_->resolved.Ready()) return;
    Impl& impl = *impl_;
    ID3D12GraphicsCommandList* list = commandList.Native();

    TaaShaderConstants constants = {};
    std::memcpy(constants.invViewProj, params.invViewProj, sizeof(constants.invViewProj));
    std::memcpy(constants.prevViewProj, params.prevViewProj, sizeof(constants.prevViewProj));
    constants.jitter[0] = params.jitterNdc[0];
    constants.jitter[1] = params.jitterNdc[1];
    constants.invSize[0] = 1.0f / static_cast<float>(impl.width);
    constants.invSize[1] = 1.0f / static_cast<float>(impl.height);
    constants.size[0] = static_cast<float>(impl.width);
    constants.size[1] = static_cast<float>(impl.height);
    constants.historyWeight = params.historyWeight;
    constants.sharpness = params.sharpness;
    constants.colorIdx = current.SrvIndex();
    constants.historyIdx = impl.history.SrvIndex();
    constants.depthIdx = DepthSrvIndex();
    constants.velocityIdx = params.velocitySrvIndex;
    constants.reset = params.resetHistory ? 1u : 0u;

    current.Transition(commandList, HdrTargetState::ShaderResource);
    impl.history.Transition(commandList, HdrTargetState::ShaderResource);
    impl.resolved.BeginScene(commandList, {});
    ID3D12DescriptorHeap* heaps[] = {impl.srvHeap.Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl.rootSig.Get());
    list->SetPipelineState(impl.pso.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(impl.width), static_cast<float>(impl.height),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(impl.width), static_cast<LONG>(impl.height)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetGraphicsRoot32BitConstants(0, kConstantDwordCount, &constants, 0);
    list->DrawInstanced(3, 1, 0, 0);

    // 解決後の HDR を履歴へも写す(解決後はレンズフレアを足すので、履歴とは別の資源)。
    impl.history.RecordCopyFrom(commandList, impl.resolved);
}

} // namespace rs::render
