#include "BloomPass.h"

#include <cstddef>
#include <cstdint>

#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>

#include <spdlog/spdlog.h>

#include "../resource/ShaderCompileFxc.h"
#include "BloomShader.h"
#include "PostProcessFormats.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// ルート署名: [0]=入力のSRV(1枠)、[1]=定数 7 DWORD、静的サンプラ s0(リニア・クランプ)。
constexpr UINT kRootParamSrvTable = 0;
constexpr UINT kRootParamConstants = 1;

// 縮小の第 level 段(0始まり)の大きさ。第0段が1/2。
std::uint32_t LevelExtent(std::uint32_t full, std::uint32_t level) {
    return std::max<std::uint32_t>(1u, full >> (level + 1));
}

struct PassConstants {
    float invDst[2];
    float invSrc[2];
    float exposure;
    std::uint32_t karis;
    float threshold;
};

// 出力先への書き方。
enum class BlendKind {
    Replace, // 上書き(縮小)
    Lerp,    // 出力先 * (1 - f) + 結果 * f(拡大の段の間)
    Add,     // 出力先 + 結果 * f(最終の加算)
};
constexpr UINT kConstantDwordCount = sizeof(PassConstants) / sizeof(std::uint32_t);
static_assert(sizeof(PassConstants) == 28 && offsetof(PassConstants, invSrc) == 8 &&
              offsetof(PassConstants, exposure) == 16 && offsetof(PassConstants, karis) == 20 &&
              offsetof(PassConstants, threshold) == 24, "BloomConstants と配置を一致させる");

} // namespace

struct BloomPass::Impl {
    ComPtr<ID3D12RootSignature> rootSig;
    ComPtr<ID3D12PipelineState> downPso;
    ComPtr<ID3D12PipelineState> upPso;   // 拡大。段の間(Lerp)
    ComPtr<ID3D12PipelineState> addPso;  // 拡大。最終の加算(Add)
    std::array<SceneHdrTarget, kLevelCount> levels;
    std::uint32_t fullWidth = 0;
    std::uint32_t fullHeight = 0;
    bool levelsReady = false;

    bool CreateRootSignature(ID3D12Device* device);
    ComPtr<ID3D12PipelineState> CreatePso(ID3D12Device* device, const char* pixelEntry, BlendKind blend);

    // src を読み dst へ書く全画面三角形を1回記録する。blendFactor が0以上なら、PSOの書き方(Lerp・Add)の係数 f に使う。
    void DrawPass(ID3D12GraphicsCommandList* list, SceneHdrTarget& src, SceneHdrTarget& dst,
                  ID3D12PipelineState* pso, const BloomParams& params, bool karis, float blendFactor);
};

bool BloomPass::Impl::CreateRootSignature(ID3D12Device* device) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER rootParams[2] = {};
    rootParams[kRootParamSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[kRootParamSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[kRootParamSrvTable].DescriptorTable.pDescriptorRanges = &range;
    rootParams[kRootParamSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rootParams[kRootParamConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[kRootParamConstants].Constants.ShaderRegister = 0;
    rootParams[kRootParamConstants].Constants.Num32BitValues = kConstantDwordCount;
    rootParams[kRootParamConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler = {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParams;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &sampler;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> signature, error;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error))) {
        if (error) spdlog::error("[bloom] rootsig: {}", static_cast<const char*>(error->GetBufferPointer()));
        return false;
    }
    return SUCCEEDED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                                 IID_PPV_ARGS(&rootSig)));
}

ComPtr<ID3D12PipelineState> BloomPass::Impl::CreatePso(ID3D12Device* device, const char* pixelEntry, BlendKind blend) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const size_t shaderSize = kBloomShader.size();
    ComPtr<ID3DBlob> vs = CompileFxcCached(kBloomShader.data(), shaderSize, "bloom.hlsl", nullptr, "VSMain", "vs_5_0",
                                           flags, "bloom");
    ComPtr<ID3DBlob> ps = CompileFxcCached(kBloomShader.data(), shaderSize, "bloom.hlsl", nullptr, pixelEntry, "ps_5_0",
                                           flags, "bloom");
    if (!vs || !ps) return nullptr;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSig.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& target = pd.BlendState.RenderTarget[0];
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                                   D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (blend != BlendKind::Replace) {
        target.BlendEnable = TRUE;
        target.SrcBlend = D3D12_BLEND_BLEND_FACTOR;
        target.DestBlend = blend == BlendKind::Lerp ? D3D12_BLEND_INV_BLEND_FACTOR : D3D12_BLEND_ONE;
        target.BlendOp = D3D12_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D12_BLEND_ONE;
        target.DestBlendAlpha = D3D12_BLEND_ZERO;
        target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;
    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)))) {
        spdlog::error("[bloom] PSO {}", pixelEntry);
        return nullptr;
    }
    return pso;
}

void BloomPass::Impl::DrawPass(ID3D12GraphicsCommandList* list, SceneHdrTarget& src, SceneHdrTarget& dst,
                               ID3D12PipelineState* pso, const BloomParams& params, bool karis,
                               float blendFactor) {
    const rs::rhi::CommandList commandList(list);
    src.Transition(commandList, HdrTargetState::ShaderResource);
    dst.Transition(commandList, HdrTargetState::RenderTarget);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = {dst.Rtv().ptr};
    list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    const D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(dst.Width()), static_cast<float>(dst.Height()),
                                     0.0f, 1.0f};
    const D3D12_RECT scissor = {0, 0, static_cast<LONG>(dst.Width()), static_cast<LONG>(dst.Height())};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->SetPipelineState(pso);
    D3D12_GPU_DESCRIPTOR_HANDLE table = {src.SrvGpuHandle()};
    list->SetGraphicsRootDescriptorTable(kRootParamSrvTable, table);

    PassConstants constants = {};
    constants.invDst[0] = 1.0f / static_cast<float>(dst.Width());
    constants.invDst[1] = 1.0f / static_cast<float>(dst.Height());
    constants.invSrc[0] = 1.0f / static_cast<float>(src.Width());
    constants.invSrc[1] = 1.0f / static_cast<float>(src.Height());
    constants.exposure = params.exposure;
    constants.karis = karis ? 1u : 0u;
    constants.threshold = params.threshold;
    list->SetGraphicsRoot32BitConstants(kRootParamConstants, kConstantDwordCount, &constants, 0);
    if (blendFactor >= 0.0f) {
        const float factor[4] = {blendFactor, blendFactor, blendFactor, blendFactor};
        list->OMSetBlendFactor(factor);
    }
    list->DrawInstanced(3, 1, 0, 0);
}

BloomPass::BloomPass() = default;
BloomPass::~BloomPass() = default;

bool BloomPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap, std::uint32_t firstSrvIndex) {
    impl_.reset();
    if (!device.Valid() || !srvHeap.Valid()) return false;
    std::unique_ptr<Impl> impl = std::make_unique<Impl>();
    ID3D12Device* native = device.Native();
    if (!impl->CreateRootSignature(native)) return false;
    impl->downPso = impl->CreatePso(native, "PSDown", BlendKind::Replace);
    impl->upPso = impl->CreatePso(native, "PSUp", BlendKind::Lerp);
    impl->addPso = impl->CreatePso(native, "PSUp", BlendKind::Add);
    if (!impl->downPso || !impl->upPso || !impl->addPso) return false;
    const float clear[3] = {0.0f, 0.0f, 0.0f};
    for (std::uint32_t i = 0; i < kLevelCount; ++i) {
        if (!impl->levels[i].Init(device, srvHeap, firstSrvIndex + i, clear)) return false;
    }
    impl_ = std::move(impl);
    return true;
}

bool BloomPass::Resize(std::uint32_t width, std::uint32_t height) {
    if (!impl_ || width == 0 || height == 0) return false;
    Impl& impl = *impl_;
    impl.levelsReady = false;
    for (std::uint32_t i = 0; i < kLevelCount; ++i) {
        if (!impl.levels[i].Resize(LevelExtent(width, i), LevelExtent(height, i))) {
            spdlog::error("[bloom] level {} resize {}x{}", i, width, height);
            return false;
        }
    }
    impl.fullWidth = width;
    impl.fullHeight = height;
    impl.levelsReady = true;
    return true;
}

bool BloomPass::Ready() const { return impl_ && impl_->levelsReady; }

void BloomPass::Record(const rs::rhi::CommandList& commandList, SceneHdrTarget& scene, const BloomParams& params) {
    if (!Ready() || !commandList.Valid() || !scene.Ready()) return;
    Impl& impl = *impl_;
    if (scene.Width() != impl.fullWidth || scene.Height() != impl.fullHeight) return;
    ID3D12GraphicsCommandList* list = commandList.Native();

    ID3D12DescriptorHeap* heaps[] = {scene.SrvHeapRef().Native()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootSignature(impl.rootSig.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);

    // 縮小: シーン → 1/2 → 1/4 → … 。最初の1段だけ、明るい部分の切り出しと Karis 平均。
    for (std::uint32_t i = 0; i < kLevelCount; ++i) {
        SceneHdrTarget& src = i == 0 ? scene : impl.levels[i - 1];
        impl.DrawPass(list, src, impl.levels[i], impl.downPso.Get(), params, i == 0, -1.0f);
    }
    // 拡大: 小さい段から1段上へ、scatter の割合で混ぜる。
    for (std::uint32_t i = kLevelCount - 1; i-- > 0;) {
        impl.DrawPass(list, impl.levels[i + 1], impl.levels[i], impl.upPso.Get(), params, false, params.scatter);
    }
    // 最終: 1/2 の結果をシーンへ strength 倍して加算する。
    impl.DrawPass(list, impl.levels[0], scene, impl.addPso.Get(), params, false, params.strength);
}

} // namespace rs::render
