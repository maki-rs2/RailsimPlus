#include "VolumetricFogSkyPass.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/DxcShaderCompiler.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

struct SkyShaderConstants {
    std::uint32_t integratedIdx;
};
constexpr UINT kConstantDwordCount = sizeof(SkyShaderConstants) / sizeof(std::uint32_t);
static_assert(sizeof(SkyShaderConstants) == 4 && offsetof(SkyShaderConstants, integratedIdx) == 0,
              "SkyConstants の uint と配置を一致させる");

constexpr const char* kSkyShader = R"HLSL(
cbuffer SkyConstants : register(b0) {
    uint gIntegratedIdx;
};
SamplerState gLinearSmp : register(s1);

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

// 全画面の三角形。主深度の遠方(Reversed-Z で0)と一致させるので、z は0にする。
VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}

// 空は格子の最後のスライスの値を使う。固定機能のブレンドが 出力.rgb + 下の色 × 出力.a にする。
float4 PSMain(VSOut i) : SV_Target {
    Texture3D<float4> integrated = ResourceDescriptorHeap[gIntegratedIdx];
    return integrated.SampleLevel(gLinearSmp, float3(i.uv, 1.0), 0);
}
)HLSL";

} // namespace

struct VolumetricFogSkyPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> pso;
};

VolumetricFogSkyPass::VolumetricFogSkyPass() = default;
VolumetricFogSkyPass::~VolumetricFogSkyPass() = default;

bool VolumetricFogSkyPass::Init(const rs::rhi::Device& device) {
    impl_.reset();
    if (!device.Valid()) return false;
    ID3D12Device* native = device.Native();
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.ShaderRegister = 0;
    param.Constants.Num32BitValues = kConstantDwordCount;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 1;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &param;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[volumetric_fog] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    if (FAILED(native->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(&impl->rootSig))))
        return false;

    DxcShaderCompiler dxc;
    ComPtr<IDxcBlob> vs, ps;
    if (!dxc.Init() || !dxc.Compile(kSkyShader, L"VSMain", L"vs_6_6", {}, "volumetric_fog", vs) ||
        !dxc.Compile(kSkyShader, L"PSMain", L"ps_6_6", {}, "volumetric_fog", ps))
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = impl->rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_ONE;
    blend.DestBlend = D3D12_BLEND_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                  D3D12_COLOR_WRITE_ENABLE_BLUE;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;
    if (FAILED(native->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&impl->pso)))) {
        spdlog::error("[volumetric_fog] 空のパスの PSO");
        return false;
    }
    impl_ = std::move(impl);
    return true;
}

void VolumetricFogSkyPass::Record(const rs::rhi::CommandList& commandList, std::uint32_t integratedSrvIndex) const {
    if (!impl_ || !commandList.Valid()) return;
    ID3D12GraphicsCommandList* list = commandList.Native();
    list->SetGraphicsRootSignature(impl_->rootSig.Get());
    list->SetPipelineState(impl_->pso.Get());
    const SkyShaderConstants constants{integratedSrvIndex};
    list->SetGraphicsRoot32BitConstants(0, kConstantDwordCount, &constants, 0);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    list->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
