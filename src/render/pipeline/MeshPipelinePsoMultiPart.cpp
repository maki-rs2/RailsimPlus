// MeshPipelinePsoMultiPart.cpp - MeshPipelineSharedState のマルチパート PSO 生成
// MeshPipelineSharedState.cpp から分離。

#include "MeshPipelineSharedState.h"

#include "MeshPipeline.h"

#include <spdlog/spdlog.h>

namespace rs::render {

bool MeshPipelineSharedState::CreateInstancedMultiPartPsos(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
    D3D12_SHADER_BYTECODE vsInstancedMultiPart, D3D12_SHADER_BYTECODE psCutoutDepthOnly,
    D3D12_SHADER_BYTECODE psTransparentWboit, D3D12_SHADER_BYTECODE psSolidFade) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pi = pd;
    pi.VS = vsInstancedMultiPart;
    if (FAILED(device->CreateGraphicsPipelineState(&pi, IID_PPV_ARGS(&psoInstancedMultiPartSolid_)))) {
        spdlog::error("[mesh] instanced multi-part solid PSO");
        return false;
    }
    pi.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    // MeshShader.hのkCutoutOccluderAlphaThreshold(render_tuning.json の値を-Dで受け取る)で残ったtexelだけを
    // 深度へ書く。カラー書込みはWriteMask=0で無効化する。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pcd = pi;
    pcd.PS = psCutoutDepthOnly;
    pcd.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pcd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    if (FAILED(device->CreateGraphicsPipelineState(
            &pcd, IID_PPV_ARGS(&psoInstancedMultiPartCutoutDepthOnly_)))) {
        spdlog::error("[mesh] instanced multi-part cutout depth-only PSO");
        return false;
    }
    // カラーパス自体はもう深度を書かない(DepthWriteMask=ZERO)。深度は
    // kCutoutOccluderAlphaThreshold以上のtexelについて上の深度専用パスだけが
    // 確定させ、閾値未満(半透明とみなす領域)は深度を残さないことで奥の
    // lateOpaque等を隠さない。ここでカラーパス側もDepthWriteMask=ALLのままだと
    // 既存の低い閾値(gMaterial.x、通常0.02)でほぼ全texelが深度を書いてしまい、
    // 深度専用パスの意味が無くなる。
    // `pi`自体は直後のlateOpaque用PSO(DepthWriteMask=ALLが必要)にも使い回すため、
    // 複製(pcc)にだけ適用する。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pcc = pi;
    pcc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateGraphicsPipelineState(&pcc, IID_PPV_ARGS(&psoInstancedMultiPartCutout_)))) {
        spdlog::error("[mesh] instanced multi-part cutout PSO");
        return false;
    }
    // lateOpaqueは専用PSOを持たず、cutoutと同じ深度専用PSO+カラーPSOの2段で描く
    // (MeshPipelineRecord.cpp RecordInstancedMultiPart)。状態要件が完全に一致する
    // ため: どちらもGREATER_EQUAL(LateOpaqueは非発光ツインと頂点・ワールド行列が
    // 一致し厳密GREATERだと常に深度負けする)、どちらも中間alphaのtexelを
    // 深度オクルーダーにしてはならない。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pw = pi;
    // 深度テストはGREATER_EQUALにする(pi由来のままでよい)。
    // Transparentバケツには共面ツイン(ChangeAlphaで点灯する灯具が非発光の
    // ベースコピーと同一頂点データを持つケース)が含まれ、厳密GREATERだと
    // 常に深度負けする。半透明パスは三角形単位で奥→手前ソート済み
    // (MeshUpload.cpp ComputeSubmeshCentersAndTransparentTris/
    // RecordTransparentInstancedMultiPart)のため、GREATER_EQUALへ緩和しても
    // 半透明同士の順序はソートが確保する(深度書込は元々OFFのまま)。
    pw.PS = psTransparentWboit;
    pw.NumRenderTargets = 2;
    pw.RTVFormats[0] = kWboitAccumFormat;
    pw.RTVFormats[1] = kWboitRevealFormat;
    // accum(加算)とreveal(乗算)でブレンド式が違うため、RT別のブレンド設定を
    // 有効にする。これがFALSEだとD3D12はRenderTarget[0]の設定を全RTへ適用し、
    // revealまで加算(ONE,ONE)になる。revealはクリア値1から始まるので必ず1以上へ
    // 育ち、合成の`final = avg*(1-reveal) + bg*reveal`が常に背景そのものになって
    // **半透明パスの描画結果が一切画面に出ない**(窓ガラス等が丸ごと消える)。
    pw.BlendState.IndependentBlendEnable = TRUE;
    pw.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pw.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pw.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    pw.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pw.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pw.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    pw.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pw.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pw.BlendState.RenderTarget[1].BlendEnable = TRUE;
    pw.BlendState.RenderTarget[1].SrcBlend = D3D12_BLEND_ZERO;
    pw.BlendState.RenderTarget[1].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
    pw.BlendState.RenderTarget[1].BlendOp = D3D12_BLEND_OP_ADD;
    pw.BlendState.RenderTarget[1].SrcBlendAlpha = D3D12_BLEND_ZERO;
    pw.BlendState.RenderTarget[1].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pw.BlendState.RenderTarget[1].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pw.BlendState.RenderTarget[1].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pw.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(device->CreateGraphicsPipelineState(&pw, IID_PPV_ARGS(&psoInstancedMultiPartTransparentWboit_)))) {
        spdlog::error("[mesh] instanced multi-part WBOIT PSO");
        return false;
    }
    // 車両まるごとのフェード(ワープ跨ぎ)用の2パス。深度書込み付きの1パスでは
    // 同一モデル内の手前/奥のフラグメントが描画順しだいで二重にブレンドされ、
    // X線のように透けて見える。先に深度だけを確定させ、カラーパスをEQUALへ
    // 限定すると、ピクセルごとに最前面の1フラグメントだけが背景と1回ブレンド
    // される。両パスは同一PS(=同一のclip条件)なのでEQUAL比較が必ず一致する。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pfd = pd;
    pfd.VS = vsInstancedMultiPart;
    pfd.PS = psSolidFade;
    pfd.BlendState.RenderTarget[0].BlendEnable = FALSE;
    pfd.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
    pfd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    if (FAILED(device->CreateGraphicsPipelineState(&pfd, IID_PPV_ARGS(&psoInstancedMultiPartFadeDepth_)))) {
        spdlog::error("[mesh] instanced multi-part fade depth PSO");
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pfc = pfd;
    pfc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    pfc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE; // プリマルチプライ済みの出力
    pfc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    pfc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pfc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pfc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    pfc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pfc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pfc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    pfc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
    if (FAILED(device->CreateGraphicsPipelineState(&pfc, IID_PPV_ARGS(&psoInstancedMultiPartFadeColor_)))) {
        spdlog::error("[mesh] instanced multi-part fade color PSO");
        return false;
    }
    return true;
}

} // namespace rs::render

