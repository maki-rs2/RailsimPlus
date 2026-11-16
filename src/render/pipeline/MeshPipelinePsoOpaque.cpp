// MeshPipelinePsoOpaque.cpp - MeshPipelineSharedState の不透明系 PSO 生成
// MeshPipelineSharedState.cpp から分離。

#include "MeshPipelineSharedState.h"

#include <spdlog/spdlog.h>

namespace rs::render {

bool MeshPipelineSharedState::CreateSolidAndCutoutPsos(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd) {
    if (FAILED(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoSolid_)))) {
        spdlog::error("[mesh] solid PSO");
        return false;
    }

    // カットアウト用 PSO: psoSolid_ と同一(ブレンドON+深度書込ON+アルファテストは
    // シェーダ側)だが、深度比較を GREATER_EQUAL にする。RailSim II は全パス LESSEQUAL で
    // 共面(ホーム黄線と床など)を「後に描いた方が勝つ」で処理している。
    // reversed-Z ではその等価は GREATER_EQUAL。cutout(穴あきテクスチャ=デカール類)を
    // 床の後に描けば、同一深度でもタイを通して床の上に乗る。float reversed-Z では
    // DepthBias が遠距離で効きが不安定なため、バイアスではなく比較緩和で解決する。
    // 不透明(psoSolid_)は厳密 GREATER のままなので、二重床同士の自己Zファイティングは
    // 誘発しない(cutout に大面積の共面ペアは通常無い)。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pc = pd;
    pc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&pc, IID_PPV_ARGS(&psoCutout_)))) {
        spdlog::error("[mesh] cutout PSO");
        return false;
    }
    return true;
}

bool MeshPipelineSharedState::CreateInstancedPsos(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
    D3D12_SHADER_BYTECODE vsInstanced, D3D12_SHADER_BYTECODE psHighlight) {
    // インスタンス描画用 PSO。psoSolid_/psoCutout_ と rootSig_・
    // InputLayout・ブレンド・深度設定を完全に同一のまま VS のみ VSMainInstanced に
    // 差し替える(コピー生成。reversed-Z GREATER/GREATER_EQUALの符号や
    // CBV_SRV_UAV_HEAP_DIRECTLY_INDEXEDフラグ等、正しさが自明でない設定を
    // 再現し直さずそのまま継承するため)。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pi = pd;
    pi.VS = vsInstanced;
    if (FAILED(device->CreateGraphicsPipelineState(&pi, IID_PPV_ARGS(&psoInstancedSolid_)))) {
        spdlog::error("[mesh] instanced solid PSO");
        return false;
    }
    pi.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&pi, IID_PPV_ARGS(&psoInstancedCutout_)))) {
        spdlog::error("[mesh] instanced cutout PSO");
        return false;
    }

    // Scenery選択ハイライト用PSO: psoInstancedSolid_と同一VS(VSMainInstanced、
    // 深度がビット単位で一致することが前提、docs参照)のまま、PSのみ
    // PSMainHighlight(フラット色出力)へ差し替え、深度は書込OFF+GREATER_EQUAL
    // (自己同一メッシュの重ね描きなので共面Zファイティングは起きない)。
    // ブレンドはpd由来のONE/INV_SRC_ALPHA(プリマルチプライ)をそのまま継承
    // (PSMainHighlightがgDiffuse.aで塗りの濃さを制御)。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC ph = pi;
    ph.PS = psHighlight;
    ph.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ph.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&ph, IID_PPV_ARGS(&psoInstancedHighlight_)))) {
        spdlog::error("[mesh] instanced highlight PSO");
        return false;
    }
    return true;
}


bool MeshPipelineSharedState::CreateHighlightPso(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
    D3D12_SHADER_BYTECODE psHighlight) {
    // 非静的Scenery(slots_側、可動部/Joint追従アニメ持ち)選択ハイライト用PSO:
    // psoSolid_と同一VS(VSMain、非インスタンス・partIndexでgPartWorldを引く)
    // のまま、PSのみPSMainHighlightへ差し替える(MeshPipeline::RecordHighlightSelf
    // 専用。同一エンティティの既存描画パスをそのまま再利用するため、静的
    // インスタンス経路と違いgPartWorldの追加書き込みは不要=SceneRenderer::Tick
    // が毎フレーム更新済みのものをそのまま使う)。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC phs = pd;
    phs.PS = psHighlight;
    phs.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    phs.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&phs, IID_PPV_ARGS(&psoHighlight_)))) {
        spdlog::error("[mesh] highlight PSO");
        return false;
    }
    return true;
}

} // namespace rs::render

