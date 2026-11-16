//
// MeshPipelineSharedState.h - 全MeshPipelineインスタンス間で共有する
// root signature + PSO一式(不透明/カットアウト/半透明/WBOIT/インスタンス描画)。
//
// device と rtvFormat(常に kSceneHdrFormat=HDR中間RTの形式)だけに依存し、per-instance状態
// (定数バッファ/テクスチャ/SRVインデックス/パーツワールド行列)を一切持たないため
// 完全に共有可能。FrameRenderer::Init時に1回だけ生成し、meshPipeline_/
// scenePipelines_[]/InstanceBatchの全MeshPipelineへ参照を配る(スロット/バッチ
// 生成のたびに生成すると、root sig生成+DXCシェーダコンパイル+PSO一式のコストが
// 大規模レイアウトのロード時に数百回分直列に積み重なる)。
//
#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <memory>

#include "../rhi/CommandList.h"
#include "../rhi/RetirementQueue.h"

namespace rs::render {

struct MeshConstants;
struct MeshPrepassPsoStore; // 深度プリパス用 PSO の入れ物(定義は MeshPipelineSharedState.cpp)
struct MeshVelocityPsoStore; // 速度パス用 PSO の入れ物(同上)

class MeshPipelineSharedState {
public:
    MeshPipelineSharedState();
    ~MeshPipelineSharedState();
    MeshPipelineSharedState(const MeshPipelineSharedState&) = delete;
    MeshPipelineSharedState& operator=(const MeshPipelineSharedState&) = delete;

    // ルートシグネチャ内の影定数(b1、ルートCBV)の位置。
    static constexpr UINT kShadowConstantsRootParam = 4;
    // ルートシグネチャ内の視点の定数(b2、ルートCBV)の位置。
    static constexpr UINT kViewConstantsRootParam = 5;

    bool Init(ID3D12Device* device, DXGI_FORMAT rtvFormat);

    // 同じ提出キューを使う全パイプラインで、資源の寿命をまとめて管理する。
    void SetRetirementQueue(rs::rhi::RetirementQueue& queue) { retirementQueue_ = &queue; }
    rs::rhi::RetirementQueue* Retirements() const { return retirementQueue_; }

    ID3D12RootSignature* RootSignature() const { return rootSig_.Get(); }
    ID3D12PipelineState* Solid() const { return psoSolid_.Get(); }
    ID3D12PipelineState* Cutout() const { return psoCutout_.Get(); }
    ID3D12PipelineState* Transparent() const { return psoTransparent_.Get(); }
    ID3D12PipelineState* TransparentWboit() const { return psoTransparentWboit_.Get(); }
    ID3D12PipelineState* InstancedSolid() const { return psoInstancedSolid_.Get(); }
    ID3D12PipelineState* InstancedCutout() const { return psoInstancedCutout_.Get(); }
    ID3D12PipelineState* InstancedTransparentWboit() const {
        return psoInstancedTransparentWboit_.Get();
    }
    ID3D12PipelineState* InstancedMultiPartSolid() const { return psoInstancedMultiPartSolid_.Get(); }
    ID3D12PipelineState* InstancedMultiPartCutoutDepthOnly() const {
        return psoInstancedMultiPartCutoutDepthOnly_.Get();
    }
    ID3D12PipelineState* InstancedMultiPartCutout() const { return psoInstancedMultiPartCutout_.Get(); }
    // LateOpaqueは専用PSOを持たず、上のCutoutDepthOnly+Cutoutを
    // そのまま使う(状態要件が一致する)。
    ID3D12PipelineState* InstancedMultiPartTransparentWboit() const { return psoInstancedMultiPartTransparentWboit_.Get(); }
    ID3D12PipelineState* InstancedMultiPartFadeDepth() const { return psoInstancedMultiPartFadeDepth_.Get(); }
    ID3D12PipelineState* InstancedMultiPartFadeColor() const { return psoInstancedMultiPartFadeColor_.Get(); }
    // Scenery選択ハイライト用(MeshPipeline::RecordHighlightInstanced専用、
    // 静的インスタンス化されたScenery/Pier/Pole向け)。
    ID3D12PipelineState* InstancedHighlight() const { return psoInstancedHighlight_.Get(); }
    // Scenery選択ハイライト用(MeshPipeline::RecordHighlightSelf専用、非静的
    // Scenery=SceneRenderer::slots_側向け。VSMain(非インスタンス、partIndexで
    // gPartWorldを引く)を使うため、可動部/Joint追従アニメを持つオブジェクトでも
    // ティントが実際の姿勢に追従する)。
    ID3D12PipelineState* Highlight() const { return psoHighlight_.Get(); }
    // シャドウパス用(深度のみ。MeshPipeline::RecordShadowInstancedMultiPart専用)。
    // Solid はピクセルシェーダなし、AlphaTest は穴あきの材質のアルファ判定だけを持つ。
    ID3D12PipelineState* ShadowSolid() const { return psoShadowSolid_.Get(); }
    ID3D12PipelineState* ShadowAlphaTest() const { return psoShadowAlphaTest_.Get(); }
    // 主視点の深度プリパス用(深度のみ。色の出力先なし。MeshPipeline::RecordPrepassInstancedMultiPart専用)。
    // 深度比較・カリング・バイアスは本編の不透明と同じ(Reversed-Z、深度バイアスなし)。alphaTest は穴あきの
    // 材質のアルファ判定を持つ側、false はピクセルシェーダなし。PSO は内部に持ち、型を外に出さない。
    void BindPrepassPso(const rs::rhi::CommandList& commandList, bool alphaTest) const;
    // 速度パス用(速度の RT と、プリパスの深度を読み取り専用で使う。MeshPipeline::RecordVelocityInstancedMultiPart専用)。
    // 深度比較は「等しいか近い」(Reversed-Z の GREATER_EQUAL)で書き込みなし。alphaTest は穴あきの材質のアルファ判定を持つ側。
    void BindVelocityPso(const rs::rhi::CommandList& commandList, bool alphaTest) const;
    // ルートシグネチャを張り、影定数(b1)と視点の定数(b2)を張る。ルートシグネチャを張り直すとb1・b2も外れるので、
    // 各 Record は SetGraphicsRootSignature を直接呼ばずこれを使う。base.shadowConstantsAddress が0
    // (影を使わない描画。サムネイルなど)なら、ゼロ初期化のバッファを張る。視点の定数に空きは無く、
    // base.viewConstantsAddress が0なら張らずにエラーを1回だけ出す(呼び出し側の設定漏れ)。
    void BindRootSignature(ID3D12GraphicsCommandList* cmdList, const MeshConstants& base) const;
    // 「影を使わない」ゼロ初期化の影定数(b1)のGPUアドレス。影を使わない描画(サムネイルなど)が張る。
    // 中身は不変なので、別スレッド・別キューで共有しても競合しない。
    D3D12_GPU_VIRTUAL_ADDRESS NullShadowConstants() const {
        return nullShadowConstants_ ? nullShadowConstants_->GetGPUVirtualAddress() : 0;
    }

private:
    // 同期待機してから差し替える撮影用パイプラインでは設定しない。
    rs::rhi::RetirementQueue* retirementQueue_ = nullptr;
    // Init のフェーズ分割。PSO は「基本記述 pd をコピーして必要な箇所だけ
    // 書き換える」形で派生させるため、生成系は一律に基本記述を受け取る
    // (reversed-Z の比較符号やルートシグネチャフラグ等、正しさが自明でない
    // 設定を各 PSO で再現し直さないため)。
    bool CreateMeshRootSignature(ID3D12Device* device);
    bool CreateSolidAndCutoutPsos(ID3D12Device* device,
                                  const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd);
    bool CreateInstancedPsos(ID3D12Device* device,
                             const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                             D3D12_SHADER_BYTECODE vsInstanced,
                             D3D12_SHADER_BYTECODE psHighlight);
    bool CreateInstancedMultiPartPsos(ID3D12Device* device,
                                      const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                                      D3D12_SHADER_BYTECODE vsInstancedMultiPart,
                                      D3D12_SHADER_BYTECODE psCutoutDepthOnly,
                                      D3D12_SHADER_BYTECODE psTransparentWboit,
                                      D3D12_SHADER_BYTECODE psSolidFade);
    bool CreateHighlightPso(ID3D12Device* device,
                            const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                            D3D12_SHADER_BYTECODE psHighlight);
    bool CreateShadowPsos(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                          D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE psAlphaTest);
    bool CreateNullShadowConstants(ID3D12Device* device);
    bool CreateTransparentPsos(ID3D12Device* device,
                               const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                               D3D12_SHADER_BYTECODE psTrans,
                               D3D12_SHADER_BYTECODE psTransWboit,
                               D3D12_SHADER_BYTECODE vsInstanced);

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoSolid_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoCutout_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoTransparent_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoTransparentWboit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedSolid_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedCutout_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedTransparentWboit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedHighlight_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartSolid_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartCutoutDepthOnly_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartCutout_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartTransparentWboit_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartFadeDepth_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoInstancedMultiPartFadeColor_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoHighlight_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoShadowSolid_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> psoShadowAlphaTest_;
    std::unique_ptr<MeshPrepassPsoStore> prepassPsos_;
    std::unique_ptr<MeshVelocityPsoStore> velocityPsos_;
    Microsoft::WRL::ComPtr<ID3D12Resource> nullShadowConstants_;
};

} // namespace rs::render
