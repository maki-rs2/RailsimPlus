// MeshPipelinePsoTransparent.cpp - MeshPipelineSharedState の半透明 PSO 生成
// MeshPipelineSharedState.cpp から分離。

#include "MeshPipelineSharedState.h"

#include "MeshPipeline.h"

#include <spdlog/spdlog.h>

namespace rs::render {


bool MeshPipelineSharedState::CreateTransparentPsos(
    ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
    D3D12_SHADER_BYTECODE psTrans, D3D12_SHADER_BYTECODE psTransWboit,
    D3D12_SHADER_BYTECODE vsInstanced) {
    // 半透明用 PSO: アルファブレンド有効・深度テストあり・深度書込なし
    // PS は動的 descriptor でマテリアル選択する PSMainTrans を使う。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pt = pd;
    pt.PS = psTrans;
    D3D12_RENDER_TARGET_BLEND_DESC& rt = pt.BlendState.RenderTarget[0];
    // プリマルチプライ済みの出力(ShadePremultiplied)を載せる。
    rt.BlendEnable = TRUE;
    rt.SrcBlend = D3D12_BLEND_ONE;
    rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = D3D12_BLEND_OP_ADD;
    rt.SrcBlendAlpha = D3D12_BLEND_ONE;
    rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    // 深度書き込みは無効（半透明描画の標準手法）。
    // 床+警告線+ほぼ透明な影、のように同一座標へ半透明レイヤーを複数枚
    // 重ねる .x 資産では、深度書き込みが有効だとソートで先に描かれた
    // 三角形が深度を書き、coplanar な残りのレイヤーが深度テストで除外
    // される。ソート順は視点角度で入れ替わるため「見る方向によって
    // メッシュが欠ける」症状になる(同一座標に半透明レイヤーを重ねる駅施設等)。
    // 書き込みを切れば半透明同士は深度で干渉せず、ブレンド順は
    // 奥→手前ソート（Record() 側）が確保する。
    pt.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    // DepthFunc は不透明パスと同じ厳密 GREATER のまま据え置く。
    // GREATER_EQUAL へ緩和すると床の大面積ポリゴン同士が視点角度依存の
    // 自己Zファイティングを起こすため、据え置く。
    //
    // 共面デカール（ホーム黄線等）対策: 半透明パス専用の DepthBias で
    // 「深度テストの比較値だけ」を手前へ寄せる（polygon offset の定石）。
    // 頂点は動かさない。深度書込は OFF なので深度バッファを
    // 汚さず、床自身の自己Zファイティングは起きない。符号に注意: reversed-Z
    // では深度値が大きいほど手前なので、GREATER を通すには比較値を「増やす」=
    // 正の DepthBias を与える（通常Zの慣習とは逆符号）。
    //
    // 値(200)を振るだけでは解決しない。このバイアスはTransparentバケツ
    // (材質色 Diffuse.a<1 か ChangeAlpha)を持つ材質「全部」へ一律に掛かるため、
    // 共面デカール(床+黄線)だけでなく物理的にオフセットされた本物のガラス材質
    // (窓ガラス・反射レイヤー等、FaceColor.a<1)も同じPSOを通る。
    // 大きくすると近距離の小さな部材が「手前に浮いて見える」過剰補正になり、
    // 小さくすると共面タイに負けて「縁がカットされる」。
    // 原因は合成(重み付けブレンド)側ではなく、深度テストの合否(採用/不採用)自体が
    // 視点角度で不安定なこと。根本的な解消手段は素材側 ChangeAlpha か PSO 分割。
    pt.RasterizerState.DepthBias = 200;              // 定数バイアス（正=手前へ）
    pt.RasterizerState.DepthBiasClamp = 0.0f;
    pt.RasterizerState.SlopeScaledDepthBias = 0.4f;  // 傾いた床(grazing角)向け
    if (FAILED(device->CreateGraphicsPipelineState(
            &pt, IID_PPV_ARGS(&psoTransparent_)))) {
        spdlog::error("[mesh] transparent PSO");
        return false;
    }

    // WBOIT 用 PSO: accum(RGBA16F) + reveal(R16F) の2RTへ出力。
    // 深度テスト/DepthBias/深度書込OFF はソート版と同じ。RTごとに別ブレンド:
    //   RT0 accum : 加算 (ONE, ONE)             → Σ(color*a, a)*w
    //   RT1 reveal: (ZERO, INV_SRC_COLOR)       → dst *= (1-a) で Π(1-a)
    // ソート不要なので Record 側の距離ソートは不要だが、実装共有のため
    // RecordTransparent は同じ動的IB(=全半透明三角形)をそのまま1ドローする。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pw = pt;
    pw.PS = psTransWboit;
    pw.NumRenderTargets = 2;
    pw.RTVFormats[0] = kWboitAccumFormat;
    pw.RTVFormats[1] = kWboitRevealFormat;
    pw.BlendState.IndependentBlendEnable = TRUE;
    D3D12_RENDER_TARGET_BLEND_DESC& a = pw.BlendState.RenderTarget[0];
    a = {};
    a.BlendEnable = TRUE;
    a.SrcBlend = D3D12_BLEND_ONE;
    a.DestBlend = D3D12_BLEND_ONE;
    a.BlendOp = D3D12_BLEND_OP_ADD;
    a.SrcBlendAlpha = D3D12_BLEND_ONE;
    a.DestBlendAlpha = D3D12_BLEND_ONE;
    a.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    a.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    D3D12_RENDER_TARGET_BLEND_DESC& r = pw.BlendState.RenderTarget[1];
    r = {};
    r.BlendEnable = TRUE;
    r.SrcBlend = D3D12_BLEND_ZERO;
    r.DestBlend = D3D12_BLEND_INV_SRC_COLOR;
    r.BlendOp = D3D12_BLEND_OP_ADD;
    r.SrcBlendAlpha = D3D12_BLEND_ZERO;
    r.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    r.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    r.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
    if (FAILED(device->CreateGraphicsPipelineState(
            &pw, IID_PPV_ARGS(&psoTransparentWboit_)))) {
        spdlog::error("[mesh] transparent WBOIT PSO");
        return false;
    }

    // インスタンス描画用 WBOIT 半透明 PSO: pw と完全同一のまま VS のみ
    // VSMainInstanced(gPartWorldをSV_InstanceIDで引く)へ差し替える。
    // WBOIT は順序非依存なので、インスタンス化(単一ドロー・任意順)と
    // 仕組み上相性が良く per-instance/per-triangle ソートが不要
    // (これが無いと静的シーナリーの半透明サブメッシュがインスタンス経路で
    // 描かれず抜ける)。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pwi = pw;
    pwi.VS = vsInstanced;
    if (FAILED(device->CreateGraphicsPipelineState(
            &pwi, IID_PPV_ARGS(&psoInstancedTransparentWboit_)))) {
        spdlog::error("[mesh] instanced transparent WBOIT PSO");
        return false;
    }
    return true;
}

} // namespace rs::render

