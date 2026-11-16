// Overlay2DPassSetup.cpp - Overlay2DPass の設定読込とパイプライン生成(シェーダ・入力レイアウト・PSO)
#include "Overlay2DPassSetup.h"

#include "../../core/ConfigJson.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "EmbeddedOverlay2D.h"
#include "../resource/ShaderCompileFxc.h"

using Microsoft::WRL::ComPtr;

namespace rs::render {
namespace {

constexpr char kOverlayShader[] = R"(
cbuffer OverlayConstants : register(b0)
{
    float2 screenSize;
    float aaSmoothingWidthPx;
    float occludedAlphaScale;
};

Texture2D<float> glyphAtlas : register(t0);
SamplerState glyphSampler : register(s0);

struct ShapeVertexInput
{
    float2 posPx : POSITION;
    float ndcZ : TEXCOORD0;
    float acrossWidth : TEXCOORD1;
    float alongPx : TEXCOORD2;
    float dashPeriodPx : TEXCOORD3;
    float dashDuty : TEXCOORD4;
    uint color : COLOR0;
};

struct ShapePixelInput
{
    float4 position : SV_POSITION;
    float acrossWidth : TEXCOORD0;
    float alongPx : TEXCOORD1;
    float dashPeriodPx : TEXCOORD2;
    float dashDuty : TEXCOORD3;
    float4 color : COLOR0;
};

struct TextVertexInput
{
    float2 posPx : POSITION;
    float ndcZ : TEXCOORD0;
    float2 uv : TEXCOORD1;
    uint color : COLOR0;
};

struct TextPixelInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

// 頂点色はRGBA8のリトルエンディアン(下位からR,G,B,A)。
float4 UnpackColor(uint color)
{
    return float4(
        color & 0xffu,
        (color >> 8u) & 0xffu,
        (color >> 16u) & 0xffu,
        (color >> 24u) & 0xffu) / 255.0;
}

float4 ToClip(float2 posPx, float ndcZ)
{
    return float4(
        posPx.x / screenSize.x * 2.0 - 1.0,
        1.0 - posPx.y / screenSize.y * 2.0,
        ndcZ,
        1.0);
}

ShapePixelInput ShapeVS(ShapeVertexInput input)
{
    ShapePixelInput output;
    output.position = ToClip(input.posPx, input.ndcZ);
    output.acrossWidth = input.acrossWidth;
    output.alongPx = input.alongPx;
    output.dashPeriodPx = input.dashPeriodPx;
    output.dashDuty = input.dashDuty;
    output.color = UnpackColor(input.color);
    return output;
}

float4 ShapePS(ShapePixelInput input) : SV_TARGET
{
    if (input.dashPeriodPx > 0.0 &&
        fmod(input.alongPx, input.dashPeriodPx) >= input.dashPeriodPx * input.dashDuty)
    {
        discard;
    }

    const float edge = 1.0 - smoothstep(
        1.0 - aaSmoothingWidthPx,
        1.0,
        abs(input.acrossWidth));
    input.color.a *= edge * occludedAlphaScale;
    return input.color;
}

TextPixelInput TextVS(TextVertexInput input)
{
    TextPixelInput output;
    output.position = ToClip(input.posPx, input.ndcZ);
    output.uv = input.uv;
    output.color = UnpackColor(input.color);
    return output;
}

// アトラスの値はグリフのカバレッジ。頂点色のアルファへ掛ける。
float4 TextPS(TextPixelInput input) : SV_TARGET
{
    input.color.a *= glyphAtlas.Sample(glyphSampler, input.uv) * occludedAlphaScale;
    return input.color;
}
)";
} // namespace

namespace {
const D3D12_INPUT_ELEMENT_DESC kShapeInputLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(OverlayVertex, x), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayVertex, ndcZ), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayVertex, acrossWidth), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 2, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayVertex, alongPx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 3, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayVertex, dashPeriodPx), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayVertex, dashDuty), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(OverlayVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};
const D3D12_INPUT_ELEMENT_DESC kTextInputLayout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(OverlayTextVertex, x), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(OverlayTextVertex, ndcZ), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(OverlayTextVertex, u), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(OverlayTextVertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};
} // namespace

namespace {

D3D12_INPUT_LAYOUT_DESC ShapeInputLayout() {
    return {kShapeInputLayout, _countof(kShapeInputLayout)};
}

D3D12_INPUT_LAYOUT_DESC TextInputLayout() {
    return {kTextInputLayout, _countof(kTextInputLayout)};
}

void SetBlendState(D3D12_BLEND_DESC& blend) {
    blend = {};
    D3D12_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D12_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
}

bool CompileShader(const char* entryPoint, const char* target, UINT flags, ID3DBlob** shader) {
    ComPtr<ID3DBlob> code = CompileFxcCached(kOverlayShader, std::strlen(kOverlayShader), "overlay2d.hlsl",
                                             nullptr, entryPoint, target, flags, "overlay2d");
    if (!code) return false;
    *shader = code.Detach();
    return true;
}

bool CreatePso(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& description,
               ComPtr<ID3D12PipelineState>& output) {
    return SUCCEEDED(device->CreateGraphicsPipelineState(&description, IID_PPV_ARGS(&output)));
}

} // namespace

bool Overlay2DPass::CreatePipelines(ID3D12Device* device, DXGI_FORMAT rtvFormat) {
    UINT flags = 0;
#if defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> shapeVs;
    ComPtr<ID3DBlob> shapePs;
    ComPtr<ID3DBlob> textVs;
    ComPtr<ID3DBlob> textPs;
    if (!CompileShader("ShapeVS", "vs_5_0", flags, &shapeVs) ||
        !CompileShader("ShapePS", "ps_5_0", flags, &shapePs) ||
        !CompileShader("TextVS", "vs_5_0", flags, &textVs) ||
        !CompileShader("TextPS", "ps_5_0", flags, &textPs)) {
        return false;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC description = {};
    description.pRootSignature = rootSig_.Get();
    description.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    description.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    description.RasterizerState.DepthClipEnable = TRUE;
    SetBlendState(description.BlendState);
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    description.SampleMask = UINT_MAX;
    description.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    description.NumRenderTargets = 1;
    description.RTVFormats[0] = rtvFormat;
    description.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    description.SampleDesc.Count = 1;
    description.VS = {shapeVs->GetBufferPointer(), shapeVs->GetBufferSize()};
    description.PS = {shapePs->GetBufferPointer(), shapePs->GetBufferSize()};
    description.InputLayout = ShapeInputLayout();
    if (!CreatePso(device, description, visiblePso_)) return false;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    if (!CreatePso(device, description, occludedPso_)) return false;
    description.DepthStencilState.DepthEnable = FALSE;
    if (!CreatePso(device, description, alwaysOnTopPso_)) return false;
    description.VS = {textVs->GetBufferPointer(), textVs->GetBufferSize()};
    description.PS = {textPs->GetBufferPointer(), textPs->GetBufferSize()};
    description.InputLayout = TextInputLayout();
    description.DepthStencilState.DepthEnable = TRUE;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    if (!CreatePso(device, description, textVisiblePso_)) return false;
    description.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    if (!CreatePso(device, description, textOccludedPso_)) return false;
    description.DepthStencilState.DepthEnable = FALSE;
    return CreatePso(device, description, textAlwaysOnTopPso_);
}

namespace overlay2d_setup {

Overlay2DPass::Settings ParseSettings() {
    Overlay2DPass::Settings settings = rs::core::ReadEmbeddedConfig<Overlay2DPass::Settings>(
        rs::embedded::kOverlay2DJson, rs::embedded::kOverlay2DJsonSize, "Overlay2D",
        [](const nlohmann::json& root) {
            Overlay2DPass::Settings settings;
            settings.occludedAlphaScale = root.value("occludedAlphaScale", settings.occludedAlphaScale);
            settings.aaSmoothingWidthPx = root.value("aaSmoothingWidthPx", settings.aaSmoothingWidthPx);
            settings.dashDuty = root.value("dashDuty", settings.dashDuty);
            settings.attenuationReferenceDistance =
                root.value("attenuationReferenceDistance", settings.attenuationReferenceDistance);
            settings.attenuationMinScale = root.value("attenuationMinScale", settings.attenuationMinScale);
            settings.textPixelSize = root.value("textPixelSize", settings.textPixelSize);
            settings.atlasInitialSize = root.value("atlasInitialSize", settings.atlasInitialSize);
            return settings;
        });
    settings.occludedAlphaScale = std::clamp(settings.occludedAlphaScale, 0.0f, 1.0f);
    settings.aaSmoothingWidthPx = std::max(settings.aaSmoothingWidthPx, 0.01f);
    settings.dashDuty = std::clamp(settings.dashDuty, 0.0f, 1.0f);
    settings.attenuationReferenceDistance = std::max(settings.attenuationReferenceDistance, 0.01f);
    settings.attenuationMinScale = std::clamp(settings.attenuationMinScale, 0.0f, 1.0f);
    settings.textPixelSize = std::max(settings.textPixelSize, 1u);
    settings.atlasInitialSize = std::max(settings.atlasInitialSize, 1u);
    return settings;
}

} // namespace overlay2d_setup

} // namespace rs::render
