// MeshPipelineSharedState.cpp - 基本記述と Init。
// シェーダコンパイル/ルートシグネチャ/PSO派生は各 .cpp へ分離。
#include "MeshPipelineSharedState.h"

#include "MeshShaderCompile.h"

#include <cstddef>

#include <spdlog/spdlog.h>

#include "../mesh/Mesh.h" // MeshVertex

namespace rs::render {

struct MeshPrepassPsoStore {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> solid;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaTest;
};

struct MeshVelocityPsoStore {
    Microsoft::WRL::ComPtr<ID3D12PipelineState> solid;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> alphaTest;
};

namespace {

// 速度 RT の形式(画面と同じ大きさ。動く物の NDC の差を xy に入れる)。SceneHdrTarget の HdrTargetFormat::Rg16Float と同じ。
constexpr DXGI_FORMAT kVelocityFormat = DXGI_FORMAT_R16G16_FLOAT;

// オフセットは MeshVertex の実メモリレイアウト（pos[3]+normal[3]+uv[2]+
// partIndex+color）に対応する: pos=0, normal=12, uv=24, partIndex=32(24+8), color=36。
static_assert(offsetof(MeshVertex, pos) == 0, "MeshVertex layout mismatch");
static_assert(offsetof(MeshVertex, normal) == 12, "MeshVertex layout mismatch");
static_assert(offsetof(MeshVertex, uv) == 24, "MeshVertex layout mismatch");
static_assert(offsetof(MeshVertex, partIndex) == 32, "MeshVertex layout mismatch");
static_assert(offsetof(MeshVertex, color) == 36, "MeshVertex layout mismatch");
static_assert(sizeof(MeshVertex) == 40, "MeshVertex layout mismatch");
// PSO記述(D3D12_GRAPHICS_PIPELINE_STATE_DESC)はInputLayoutをポインタで保持する
// だけなので、記述をコピー・受け渡しする間ずっと生存している必要がある。関数
// ローカルに置くと派生先へ渡した時点でぶら下がるため、名前空間スコープに置く。
const D3D12_INPUT_ELEMENT_DESC kMeshInputLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32_UINT, 0, 32,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 36,
     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};


// 全PSOの派生元になる基本記述(不透明・ソリッド)を組み立てる。
D3D12_GRAPHICS_PIPELINE_STATE_DESC MakeBaseMeshPsoDesc(ID3D12RootSignature* rootSig,
                                                       DXGI_FORMAT rtvFormat,
                                                       D3D12_SHADER_BYTECODE vs,
                                                       D3D12_SHADER_BYTECODE ps) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.InputLayout = {kMeshInputLayout, _countof(kMeshInputLayout)};
    pd.pRootSignature = rootSig;
    pd.VS = vs;
    pd.PS = ps;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    // RailSim II(graphic.cpp InitRenderState)は全材質を alpha blend ON かつ Z-write ON
    // で描く。不透明/cutout パスも同様にブレンド有効にし、テクスチャの per-texel
    // alpha で不透明(255)/半透明(中間)を表現する。深度書込は ON のままなので
    // 不透明 texel が背後(車内 Inside 等)を正しく遮蔽する(透けの本質は深度書込の
    // 有無であってブレンドの有無ではない)。alpha==0 の穴はシェーダ側 alpha test で
    // クリップする(cutout 材質のみ)。
    {
        D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
        // シェーダ(ShadePremultiplied)がプリマルチプライ済みの色を出す。アルファは拡散側にだけ
        // 掛かっていて、鏡面(映り込み)はアルファで薄まらない。
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;
        rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    // Reversed-Z: 深度値は近いほど1、遠いほど0になるためGREATERで比較
    // （近距離側に浮動小数点精度を集中させずフラットに配分し、
    // 遠方でのZファイティングを抑える）。
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = rtvFormat;
    pd.SampleDesc.Count = 1;
    return pd;
}

// 基本記述から、色の出力を無くして深度だけにした記述を派生させる。深度の比較(Reversed-Z の GREATER)・
// 書込み・カリング・バイアスなしは基本記述のまま。頂点シェーダも本編と同じなので、深度値は本編と一致する。
std::unique_ptr<MeshPrepassPsoStore> CreatePrepassPsos(ID3D12Device* device,
                                                       const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                                                       D3D12_SHADER_BYTECODE vs,
                                                       D3D12_SHADER_BYTECODE psAlphaTest) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC prepass = pd;
    prepass.VS = vs;
    prepass.PS = {};
    prepass.NumRenderTargets = 0;
    prepass.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    prepass.BlendState.RenderTarget[0] = {};
    std::unique_ptr<MeshPrepassPsoStore> store = std::make_unique<MeshPrepassPsoStore>();
    if (FAILED(device->CreateGraphicsPipelineState(&prepass, IID_PPV_ARGS(&store->solid)))) {
        spdlog::error("[mesh] prepass solid PSO");
        return nullptr;
    }
    prepass.PS = psAlphaTest;
    if (FAILED(device->CreateGraphicsPipelineState(&prepass, IID_PPV_ARGS(&store->alphaTest)))) {
        spdlog::error("[mesh] prepass alpha-test PSO");
        return nullptr;
    }
    return store;
}

// 基本記述から、速度 RT へ書く記述を派生させる。深度はプリパスのものを読み取り専用で使い、同じ面(等しいか近い)だけ通す。
std::unique_ptr<MeshVelocityPsoStore> CreateVelocityPsos(ID3D12Device* device,
                                                         const D3D12_GRAPHICS_PIPELINE_STATE_DESC& pd,
                                                         D3D12_SHADER_BYTECODE vs, D3D12_SHADER_BYTECODE ps,
                                                         D3D12_SHADER_BYTECODE psAlphaTest) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC velocity = pd;
    velocity.VS = vs;
    velocity.PS = ps;
    velocity.NumRenderTargets = 1;
    velocity.RTVFormats[0] = kVelocityFormat;
    velocity.BlendState.RenderTarget[0] = {};
    velocity.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    velocity.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    velocity.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    std::unique_ptr<MeshVelocityPsoStore> store = std::make_unique<MeshVelocityPsoStore>();
    if (FAILED(device->CreateGraphicsPipelineState(&velocity, IID_PPV_ARGS(&store->solid)))) {
        spdlog::error("[mesh] velocity solid PSO");
        return nullptr;
    }
    velocity.PS = psAlphaTest;
    if (FAILED(device->CreateGraphicsPipelineState(&velocity, IID_PPV_ARGS(&store->alphaTest)))) {
        spdlog::error("[mesh] velocity alpha-test PSO");
        return nullptr;
    }
    return store;
}

} // namespace

MeshPipelineSharedState::MeshPipelineSharedState() = default;
MeshPipelineSharedState::~MeshPipelineSharedState() = default;

void MeshPipelineSharedState::BindPrepassPso(const rs::rhi::CommandList& commandList, bool alphaTest) const {
    if (!prepassPsos_) return;
    commandList.Native()->SetPipelineState(alphaTest ? prepassPsos_->alphaTest.Get() : prepassPsos_->solid.Get());
}

void MeshPipelineSharedState::BindVelocityPso(const rs::rhi::CommandList& commandList, bool alphaTest) const {
    if (!velocityPsos_) return;
    commandList.Native()->SetPipelineState(alphaTest ? velocityPsos_->alphaTest.Get() : velocityPsos_->solid.Get());
}

bool MeshPipelineSharedState::Init(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    if (!CreateMeshRootSignature(device)) return false;

    // shはここで生存させ続ける(PSO記述がバイトコードをポインタで参照するため、
    // 全PSOの生成が終わるまで解放してはいけない)。
    MeshShaderBlobs sh;
    if (!CompileMeshShaders(sh)) return false;

    // 以降は基本記述pdをコピーして必要な箇所だけ書き換える形で派生させる。
    // reversed-Zの比較符号・ブレンド設定・ルートシグネチャフラグ等、正しさが
    // 自明でない設定を各PSOで再現し直さずそのまま継承するため。
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd =
        MakeBaseMeshPsoDesc(rootSig_.Get(), rtvFormat, Bytecode(sh.vsInstancedMultiPart), Bytecode(sh.ps));

    if (!CreateSolidAndCutoutPsos(device, pd)) return false;
    if (!CreateInstancedPsos(device, pd, Bytecode(sh.vsInstancedMultiPart), Bytecode(sh.psHighlight)))
        return false;
    if (!CreateInstancedMultiPartPsos(device, pd, Bytecode(sh.vsInstancedMultiPart),
                                      Bytecode(sh.psCutoutDepthOnly),
                                      Bytecode(sh.psTransWboit),
                                      Bytecode(sh.psSolidFade))) return false;
    if (!CreateHighlightPso(device, pd, Bytecode(sh.psHighlight))) return false;
    if (!CreateTransparentPsos(device, pd, Bytecode(sh.psTrans), Bytecode(sh.psTransWboit),
                               Bytecode(sh.vsInstancedMultiPart)))
        return false;
    if (!CreateShadowPsos(device, pd, Bytecode(sh.vsInstancedMultiPart),
                          Bytecode(sh.psCutoutDepthOnly)))
        return false;
    prepassPsos_ = CreatePrepassPsos(device, pd, Bytecode(sh.vsInstancedMultiPart), Bytecode(sh.psCutoutDepthOnly));
    if (!prepassPsos_) return false;
    velocityPsos_ = CreateVelocityPsos(device, pd, Bytecode(sh.vsVelocity), Bytecode(sh.psVelocity),
                                       Bytecode(sh.psVelocityAlphaTest));
    if (!velocityPsos_) return false;
    return CreateNullShadowConstants(device);
}

} // namespace rs::render
