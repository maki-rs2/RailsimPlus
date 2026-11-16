#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/DxcShaderCompiler.h"
#include "ParticleShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {
namespace {

ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device) {
    using namespace particle_detail;
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kMaxParticleTextures;
    textureRange.BaseShaderRegister = 1;
    textureRange.OffsetInDescriptorsFromTableStart = 0;
    D3D12_ROOT_PARAMETER params[3] = {};
    params[kRootInstances].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    params[kRootInstances].Descriptor.ShaderRegister = 0;
    params[kRootInstances].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[kRootTextures].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTextures].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootTextures].DescriptorTable.pDescriptorRanges = &textureRange;
    params[kRootTextures].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kRootConstants].Constants.ShaderRegister = 0;
    params[kRootConstants].Constants.Num32BitValues = kConstantDwords;
    params[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
        if (error) spdlog::error("[particle] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return nullptr;
    }
    ComPtr<ID3D12RootSignature> rootSig;
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSig))))
        return nullptr;
    return rootSig;
}

} // namespace

bool CreateParticlePipeline(ID3D12Device* device, ComPtr<ID3D12RootSignature>& rootSig,
                              ComPtr<ID3D12PipelineState>& pso) {
    rootSig = CreateRootSignature(device);
    if (!rootSig) return false;
    DxcShaderCompiler compiler;
    if (!compiler.Init()) return false;
    const std::string source = BuildParticleShader();
    ComPtr<IDxcBlob> vs, ps;
    if (!compiler.Compile(source, L"VSMain", L"vs_6_6", {}, "particle", vs) ||
        !compiler.Compile(source, L"PSMain", L"ps_6_6", {}, "particle", ps)) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlend = blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        spdlog::error("[particle] PSO creation failed");
        return false;
    }
    pso->SetName(L"Particle");
    return true;
}

} // namespace rs::render
