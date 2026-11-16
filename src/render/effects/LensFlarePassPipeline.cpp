//
// LensFlarePassPipeline.cpp - レンズフレアのパスのルート署名とPSO(LensFlarePass::Init から呼ぶ)
//
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <string>

#include <spdlog/spdlog.h>

#include "../postprocess/PostProcessFormats.h"
#include "../resource/ShaderCompileFxc.h"
#include "LensFlareGpuTypes.h"
#include "LensFlareShader.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// 描画先は、リニアHDRのシーンターゲットと同じ形式。
constexpr DXGI_FORMAT kFlareTargetFormat = kSceneHdrFormat;
constexpr UINT kDepthSrvCount = 1;

ComPtr<ID3D12RootSignature> CreateRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE depthRange = {};
    depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    depthRange.NumDescriptors = kDepthSrvCount;
    depthRange.BaseShaderRegister = 2;
    depthRange.OffsetInDescriptorsFromTableStart = 0;
    D3D12_DESCRIPTOR_RANGE textureRange = {};
    textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureRange.NumDescriptors = kMaxFlareTextures;
    textureRange.BaseShaderRegister = 2 + kDepthSrvCount;
    textureRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE transmittanceRange = {};
    transmittanceRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    transmittanceRange.NumDescriptors = 1;
    transmittanceRange.BaseShaderRegister = kTransmittanceRegister;
    transmittanceRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[6] = {};
    for (UINT slot = 0; slot < 2; ++slot) {  // t0 光源、t1 要素(どちらも頂点シェーダが読む。要素はPSも読む)
        params[slot].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[slot].Descriptor.ShaderRegister = slot;
        params[slot].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    params[kRootDepthTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootDepthTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootDepthTable].DescriptorTable.pDescriptorRanges = &depthRange;
    params[kRootDepthTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;  // 頂点シェーダが可視度で読む
    params[kRootTextureTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTextureTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootTextureTable].DescriptorTable.pDescriptorRanges = &textureRange;
    params[kRootTextureTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[kRootTransmittanceTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootTransmittanceTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootTransmittanceTable].DescriptorTable.pDescriptorRanges = &transmittanceRange;
    params[kRootTransmittanceTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[kRootConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kRootConstants].Constants.ShaderRegister = 0;
    params[kRootConstants].Constants.Num32BitValues = kFlareConstantDwords;
    params[kRootConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = _countof(params);
    desc.pParameters = params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> blob, error;
    if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
        if (error) spdlog::error("[lens_flare] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return nullptr;
    }
    ComPtr<ID3D12RootSignature> rootSig;
    if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig))))
        return nullptr;
    return rootSig;
}

// 加算(SrcAlpha×Src + One×Dest)か、アルファブレンドか。どちらも出力先のアルファは変えない。
ComPtr<ID3D12PipelineState> CreatePso(ID3D12Device* device, ID3D12RootSignature* rootSig, ID3DBlob* vs,
                                      ID3DBlob* ps, D3D12_BLEND destBlend, const char* label) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSig;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pd.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = destBlend;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blend.DestBlendAlpha = D3D12_BLEND_ONE;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;  // 深度は読むだけ(テクスチャとして)。深度テストも書き込みもしない
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kFlareTargetFormat;
    pd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        spdlog::error("[lens_flare] PSO {}", label);
        return nullptr;
    }
    return pso;
}

ComPtr<ID3DBlob> Compile(const char* entry, const char* target) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    static const std::string kSource = BuildLensFlareShader();
    return CompileFxcCached(kSource.data(), kSource.size(), "lens_flare.hlsl", nullptr, entry, target, flags,
                            "lens_flare");
}

} // namespace

// LensFlarePass.cpp の中で同じ宣言を持つ(DX12型を公開・内部ヘッダに出さないため、ヘッダは置かない)。
bool CreateLensFlarePipelines(ID3D12Device* device, ComPtr<ID3D12RootSignature>& rootSig,
                              ComPtr<ID3D12PipelineState>& elementPso, ComPtr<ID3D12PipelineState>& whiteoutPso) {
    rootSig = CreateRootSignature(device);
    if (!rootSig) return false;
    ComPtr<ID3DBlob> elementVs = Compile("VSElement", "vs_5_0");
    ComPtr<ID3DBlob> elementPs = Compile("PSElement", "ps_5_0");
    ComPtr<ID3DBlob> whiteoutVs = Compile("VSWhiteout", "vs_5_0");
    ComPtr<ID3DBlob> whiteoutPs = Compile("PSWhiteout", "ps_5_0");
    if (!elementVs || !elementPs || !whiteoutVs || !whiteoutPs) return false;
    elementPso = CreatePso(device, rootSig.Get(), elementVs.Get(), elementPs.Get(), D3D12_BLEND_ONE, "element");
    whiteoutPso =
        CreatePso(device, rootSig.Get(), whiteoutVs.Get(), whiteoutPs.Get(), D3D12_BLEND_INV_SRC_ALPHA, "whiteout");
    return elementPso && whiteoutPso;
}

} // namespace rs::render
