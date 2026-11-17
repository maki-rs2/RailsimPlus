// MeshPipelinePsoShadow.cpp - MeshPipelineSharedState のシャドウパス用 PSO と、影定数(b1)の張り方
// MeshPipelineSharedState.cpp から分離。

#include "MeshPipelineSharedState.h"
#include "../resource/BindlessDescriptors.inl"

#include <array>

#include <spdlog/spdlog.h>

#include "MeshPipelineTypes.h"
#include "../resource/RenderTuning.h"
#include "../shadow/ShadowConstants.h"

namespace rs::render {

namespace {

// render_tuning.json の shadowCullMode(0=なし、1=表面を捨てる、2=裏面を捨てる)に対応するカリング。
constexpr std::array<D3D12_CULL_MODE, 3> kShadowCullModes = {
    D3D12_CULL_MODE_NONE, D3D12_CULL_MODE_FRONT, D3D12_CULL_MODE_BACK};

D3D12_CULL_MODE ShadowCullModeFromTuning(int index) {
    if (index < 0 || index >= static_cast<int>(kShadowCullModes.size())) return D3D12_CULL_MODE_NONE;
    return kShadowCullModes[static_cast<std::size_t>(index)];
}

} // namespace

bool MeshPipelineSharedState::CreateShadowPsos(ID3D12Device* device,
                                               const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                                               D3D12_SHADER_BYTECODE vs,
                                               D3D12_SHADER_BYTECODE psAlphaTest) {
    const RenderTuning& tuning = RenderTuning::Embedded();

    // 基本記述から、色の出力を無くして深度だけにした記述を派生させる。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadow = pd;
    shadow.VS = vs;
    shadow.PS = {};
    shadow.NumRenderTargets = 0;
    shadow.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    shadow.BlendState.RenderTarget[0] = {};
    // 通常方向の深度(近=0、遠=1)。主深度バッファの Reversed-Z とは独立。
    shadow.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadow.RasterizerState.CullMode = ShadowCullModeFromTuning(tuning.shadowCullMode);
    shadow.RasterizerState.DepthBias = tuning.shadowDepthBias;
    shadow.RasterizerState.SlopeScaledDepthBias = tuning.shadowSlopeScaledDepthBias;
    shadow.RasterizerState.DepthBiasClamp = 0.0f;
    // ライトの手前側(ライトの近クリップより外)にある影を落とすものは、切り捨てずに近クリップ面へ寄せて残す。
    shadow.RasterizerState.DepthClipEnable = FALSE;

    if (FAILED(device->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&psoShadowSolid_)))) {
        spdlog::error("[mesh] shadow solid PSO");
        return false;
    }
    shadow.PS = psAlphaTest;
    if (FAILED(device->CreateGraphicsPipelineState(&shadow, IID_PPV_ARGS(&psoShadowAlphaTest_)))) {
        spdlog::error("[mesh] shadow alpha-test PSO");
        return false;
    }
    return true;
}

void MeshPipelineSharedState::BindRootSignature(const rs::rhi::CommandList& commandList,
                                                const MeshConstants& base, const rs::rhi::SrvHeap& srvHeap) const {
    ID3D12GraphicsCommandList* cmdList = commandList.Native();
    cmdList->SetGraphicsRootSignature(rootSig_.Get());
    SetGraphicsBindlessTable(cmdList, kBindlessRootParam, srvHeap.Native());
    cmdList->SetGraphicsRootConstantBufferView(
        kShadowConstantsRootParam,
        base.shadowConstantsAddress != 0 ? base.shadowConstantsAddress : NullShadowConstants());
    if (base.viewConstantsAddress != 0) {
        cmdList->SetGraphicsRootConstantBufferView(kViewConstantsRootParam, base.viewConstantsAddress);
        return;
    }
    static bool warned = false;
    if (!warned) {
        spdlog::error("[mesh] viewConstantsAddress が未設定のまま描画しようとしています");
        warned = true;
    }
}

bool MeshPipelineSharedState::CreateNullShadowConstants(ID3D12Device* device) {
    // 中身はゼロのまま(shadowParams.x=0 で影の分岐を通らない)。UPLOADヒープのバッファは作成時に0で埋まる。
    if (!CreatePooledUploadBuffer(device, sizeof(ShadowConstants), nullShadowConstants_)) {
        spdlog::error("[mesh] null shadow constants");
        return false;
    }
    return true;
}

} // namespace rs::render
