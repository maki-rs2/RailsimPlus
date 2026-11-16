#include "WboitPass.h"

#include <d3dcompiler.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include <d3d12.h>
#include <wrl/client.h>

#include "../pipeline/MeshPipeline.h" // kWboitAccumFormat / kWboitRevealFormat
#include "../pipeline/ShaderColor.h"
#include "../postprocess/PostProcessFormats.h"
#include "../postprocess/SceneHdrTarget.h"
#include "../resource/ShaderCompileFxc.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {

namespace {

// 全画面三角形で accum/reveal をシーンターゲットへ合成する最小シェーダ。
// VS は SV_VertexID から巨大三角形を生成、PS は Load(texelFetch)で該当ピクセルを読む。
// RailSim II は固定機能でエンコード済み(ガンマ空間)の値のまま混ぜるので、背景と半透明の平均色を
// sRGBへ変換して reveal で混ぜ、リニアへ戻す。背景は合成前のシーンのコピー(t2)。
// ハードウェアブレンドは使わず、最終色を直接出す。
const char* kCompositeShaderBody = R"(
Texture2D<float4> gAccum  : register(t0);
Texture2D<float>  gReveal : register(t1);
Texture2D<float4> gBackground : register(t2);

struct VSOut { float4 pos : SV_POSITION; };

VSOut VSMain(uint vid : SV_VertexID) {
    VSOut o;
    float2 p = float2((vid << 1) & 2, vid & 2); // (0,0),(2,0),(0,2)
    o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    int2 pix = int2(i.pos.xy);
    float4 accum = gAccum.Load(int3(pix, 0));
    float reveal = gReveal.Load(int3(pix, 0));
    float4 bg = gBackground.Load(int3(pix, 0));
    // 何も描かれていない画素は背景をそのまま返す(往復変換の誤差を出さない)。
    if (reveal >= 1.0) return bg;
    float3 avg = accum.rgb / max(accum.a, 1e-5);
    float3 mixedSrgb = SrgbFromLinear(avg) * (1.0 - reveal) + SrgbFromLinear(bg.rgb) * reveal;
    return float4(LinearFromSrgb(mixedSrgb), bg.a);
}
)";

} // namespace

struct WboitPass::Impl {
    bool Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize, UINT accumSrvIndex);
    bool CreateCompositePipeline(ID3D12Device* device, DXGI_FORMAT backBufferFormat);
    bool CreateTargets(ID3D12Device* device, UINT width, UINT height);
    bool Ready() const { return accum_ && reveal_ && background_ && compositePso_; }

    ComPtr<ID3D12Resource>       accum_;
    ComPtr<ID3D12Resource>       reveal_;
    ComPtr<ID3D12Resource>       background_; // 合成前のシーンのコピー
    DXGI_FORMAT                  sceneFormat_ = DXGI_FORMAT_UNKNOWN;
    ComPtr<ID3D12DescriptorHeap> rtvHeap_; // accum/reveal 用（2枚）
    UINT                         rtvSize_ = 0;

    ComPtr<ID3D12RootSignature>  compositeRootSig_;
    ComPtr<ID3D12PipelineState>  compositePso_;

    ID3D12DescriptorHeap* srvHeap_ = nullptr;
    UINT                  srvSize_ = 0;
    UINT                  accumSrvIndex_ = 0; // reveal は +1、背景コピーは +2
};

WboitPass::WboitPass() : impl_(std::make_unique<Impl>()) {}
WboitPass::~WboitPass() = default;

bool WboitPass::Init(const rs::rhi::Device& device, const rs::rhi::SrvHeap& srvHeap,
                     std::uint32_t accumSrvIndex) {
    return impl_->Init(device.Native(), srvHeap.Native(), srvHeap.Stride(), accumSrvIndex);
}

bool WboitPass::CreateTargets(const rs::rhi::Device& device, std::uint32_t width, std::uint32_t height) {
    return impl_->CreateTargets(device.Native(), width, height);
}

bool WboitPass::Ready() const { return impl_->Ready(); }

bool WboitPass::Impl::Init(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                           UINT accumSrvIndex) {
    srvHeap_ = srvHeap;
    srvSize_ = srvSize;
    accumSrvIndex_ = accumSrvIndex;
    const DXGI_FORMAT backBufferFormat = kSceneHdrFormat;
    sceneFormat_ = backBufferFormat;

    D3D12_DESCRIPTOR_HEAP_DESC rh = {};
    rh.NumDescriptors = 2; // accum, reveal
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&rtvHeap_)))) {
        spdlog::error("[wboit] rtv heap");
        return false;
    }
    rtvSize_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    return CreateCompositePipeline(device, backBufferFormat);
}

bool WboitPass::Impl::CreateCompositePipeline(ID3D12Device* device, DXGI_FORMAT backBufferFormat) {
    // ルートシグネチャ: t0=accum, t1=reveal, t2=背景コピー の3SRVを1テーブルで（サンプラ不要=Load）。
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = kSrvCount;
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER param = {};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rs = {};
    rs.NumParameters = 1;
    rs.pParameters = &param;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                           &err))) {
        if (err) spdlog::error("[wboit] rootsig: {}",
                               (const char*)err->GetBufferPointer());
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                                           sig->GetBufferSize(),
                                           IID_PPV_ARGS(&compositeRootSig_))))
        return false;

    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    const std::string shaderSource = std::string(kSrgbTransferShader) + kCompositeShaderBody;
    ComPtr<ID3DBlob> vs = CompileFxcCached(shaderSource.c_str(), shaderSource.size(), "wboit_composite.hlsl",
                                           nullptr, "VSMain", "vs_5_0", flags, "wboit");
    ComPtr<ID3DBlob> ps = CompileFxcCached(shaderSource.c_str(), shaderSource.size(), "wboit_composite.hlsl",
                                           nullptr, "PSMain", "ps_5_0", flags, "wboit");
    if (!vs || !ps) return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = compositeRootSig_.Get();
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    // 背景はシェーダが読んで混ぜるので、ブレンドは使わず最終色をそのまま書く。
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    rt.BlendEnable = FALSE;
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.DepthStencilState.StencilEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = backBufferFormat;
    pd.SampleDesc.Count = 1;
    if (FAILED(device->CreateGraphicsPipelineState(&pd,
                                                   IID_PPV_ARGS(&compositePso_)))) {
        spdlog::error("[wboit] composite PSO");
        return false;
    }
    return true;
}

bool WboitPass::Impl::CreateTargets(ID3D12Device* device, UINT width, UINT height) {
    if (width == 0 || height == 0) return false;
    accum_.Reset();
    reveal_.Reset();
    background_.Reset();

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    auto makeRt = [&](DXGI_FORMAT fmt, const float clear[4],
                      ComPtr<ID3D12Resource>& out) -> bool {
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = width;
        rd.Height = height;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = fmt;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv = {};
        cv.Format = fmt;
        std::memcpy(cv.Color, clear, sizeof(float) * 4);
        // 生成直後は合成の入力(SRV)状態にしておく。毎フレーム Begin が
        // PIXEL_SHADER_RESOURCE→RENDER_TARGET へ、Composite が逆へ遷移する。
        return SUCCEEDED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cv, IID_PPV_ARGS(&out)));
    };

    const float accumClear[4] = {0, 0, 0, 0};
    const float revealClear[4] = {1, 0, 0, 0}; // reveal=Π(1-a) は1で初期化
    if (!makeRt(kWboitAccumFormat, accumClear, accum_)) {
        spdlog::error("[wboit] accum target");
        return false;
    }
    if (!makeRt(kWboitRevealFormat, revealClear, reveal_)) {
        spdlog::error("[wboit] reveal target");
        return false;
    }

    // 背景コピー先(シーンターゲットと同じ大きさ・形式)。普段は合成の入力(SRV)状態で待機する。
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    bd.Width = width;
    bd.Height = height;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.Format = sceneFormat_;
    bd.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                                               IID_PPV_ARGS(&background_)))) {
        spdlog::error("[wboit] background copy target");
        return false;
    }

    // RTV（accum=slot0, reveal=slot1）
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(accum_.Get(), nullptr, rtv);
    rtv.ptr += rtvSize_;
    device->CreateRenderTargetView(reveal_.Get(), nullptr, rtv);

    // SRV（合成入力。accumSrvIndex_ からの連続 kSrvCount 枠）
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE srv = srvHeap_->GetCPUDescriptorHandleForHeapStart();
    srv.ptr += SIZE_T(accumSrvIndex_) * srvSize_;
    sd.Format = kWboitAccumFormat;
    device->CreateShaderResourceView(accum_.Get(), &sd, srv);
    srv.ptr += srvSize_;
    sd.Format = kWboitRevealFormat;
    device->CreateShaderResourceView(reveal_.Get(), &sd, srv);
    srv.ptr += srvSize_;
    sd.Format = sceneFormat_;
    device->CreateShaderResourceView(background_.Get(), &sd, srv);
    return true;
}

void WboitPass::Begin(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor depthDsvSlot) {
    Impl& s = *impl_;
    if (!s.Ready()) return;
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE depthDsv = {depthDsvSlot.ptr};

    D3D12_RESOURCE_BARRIER toRt[2] = {};
    for (int i = 0; i < 2; ++i) {
        toRt[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRt[i].Transition.pResource = (i == 0 ? s.accum_ : s.reveal_).Get();
        toRt[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toRt[i].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRt[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(2, toRt);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = s.rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {rtv0, rtv0};
    rtvs[1].ptr += s.rtvSize_;
    // 深度は不透明パスで書いた内容をテスト専用(書込マスクZERO)で使う。
    cmdList->OMSetRenderTargets(2, rtvs, FALSE, depthDsv.ptr ? &depthDsv : nullptr);

    const float accumClear[4] = {0, 0, 0, 0};
    const float revealClear[4] = {1, 0, 0, 0};
    cmdList->ClearRenderTargetView(rtvs[0], accumClear, 0, nullptr);
    cmdList->ClearRenderTargetView(rtvs[1], revealClear, 0, nullptr);
}

void WboitPass::Composite(const rs::rhi::CommandList& commandList, rs::rhi::CpuDescriptor backBufferRtvSlot,
                          SceneHdrTarget& sceneTarget) {
    Impl& s = *impl_;
    if (!s.Ready()) return;
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv = {backBufferRtvSlot.ptr};

    D3D12_RESOURCE_BARRIER toSrv[2] = {};
    for (int i = 0; i < 2; ++i) {
        toSrv[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrv[i].Transition.pResource = (i == 0 ? s.accum_ : s.reveal_).Get();
        toSrv[i].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toSrv[i].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toSrv[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    }
    cmdList->ResourceBarrier(2, toSrv);

    // 合成前のシーンを背景コピーへ写す(シーンターゲットは読みながら書けないため)。
    D3D12_RESOURCE_BARRIER copyBarrier = {};
    copyBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    copyBarrier.Transition.pResource = s.background_.Get();
    copyBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    copyBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    copyBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmdList->ResourceBarrier(1, &copyBarrier);
    const bool copied = sceneTarget.RecordCopyTo(commandList, s.background_.Get());
    std::swap(copyBarrier.Transition.StateBefore, copyBarrier.Transition.StateAfter);
    cmdList->ResourceBarrier(1, &copyBarrier);
    if (!copied) {
        spdlog::error("[wboit] シーンターゲットのコピーに失敗(大きさか形式が合わない)");
        return;
    }

    cmdList->OMSetRenderTargets(1, &backBufferRtv, FALSE, nullptr);
    cmdList->SetGraphicsRootSignature(s.compositeRootSig_.Get());
    cmdList->SetPipelineState(s.compositePso_.Get());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);

    D3D12_GPU_DESCRIPTOR_HANDLE table = s.srvHeap_->GetGPUDescriptorHandleForHeapStart();
    table.ptr += SIZE_T(s.accumSrvIndex_) * s.srvSize_;
    cmdList->SetGraphicsRootDescriptorTable(0, table);

    cmdList->DrawInstanced(3, 1, 0, 0);
}

} // namespace rs::render
