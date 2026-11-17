// MeshShaderCompile.cpp - MeshPipelineSharedState のシェーダコンパイル
// MeshPipelineSharedState.cpp から分離。

#include "MeshShaderCompile.h"

#include "../../platform/EnvironmentVariable.h"
#include "MeshShader.h"
#include "../resource/RenderTuning.h"
#include "../resource/DxcShaderCompiler.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace rs::render {

using Microsoft::WRL::ComPtr;

namespace {

// HLSLへ-Dで渡す調整値の一覧(名前, 値)。順序はキャッシュキーに入る。
// 調整値は呼ばれた時点で読む(起動時に読み直した値を使うため、静的初期化では読まない)。
std::vector<std::wstring> BuildDefineArguments() {
    const RenderTuning& tuning = RenderTuning::Embedded();
    const std::pair<const wchar_t*, float> defines[] = {
        {L"KCUTOUT_OCCLUDER_ALPHA_THRESHOLD", tuning.cutoutOccluderAlphaThreshold},
        {L"KSEMI_ALPHA_LOW", tuning.semiAlphaLow},
        {L"KAMBIENT_INTENSITY", tuning.ambientIntensity},
        {L"KNIGHT_AMBIENT_INTENSITY", tuning.nightAmbientIntensity},
        {L"KNIGHT_AMBIENT_FLOOR", tuning.nightAmbientFloor},
        {L"KSPECULAR_AA_STRENGTH", tuning.specularAaStrength},
        {L"KDEFAULT_ROUGHNESS", tuning.defaultRoughness},
        {L"KDEFAULT_METALLIC", tuning.defaultMetallic},
        {L"KSPECULAR_F0_SCALE", tuning.specularF0Scale},
        {L"KPOWER_ROUGHNESS_SCALE", tuning.powerRoughnessScale},
        {L"KENVSPEC_METALLIC", tuning.envSpecMetallic},
        {L"KENVSPEC_ROUGHNESS", tuning.envSpecRoughness},
    };
    std::vector<std::wstring> result;
    for (const std::pair<const wchar_t*, float>& define : defines) {
        result.push_back(std::wstring(define.first) + L"=" + std::to_wstring(define.second));
    }
    // 診断用(一時): 反射光が乏しい照明条件でもORM/法線テクスチャの
    // 適用有無・実際のサンプル内容を判別できるよう、環境変数RS_DEBUG_ORM_TINTが
    // 設定されている間はシェーディング結果を差し替える(MeshShader.h側の
    // RS_DEBUG_ORM_TINT定義を参照)。値で診断モードを切り替える:
    // 1=ORM/法線の適用有無を色分け(既定)、2=法線マップの生テクセルをそのまま
    // 表示(誤ったスロット参照の検出用)、3=ORMマップの生テクセルをそのまま表示。
    if (const char* v = rs::platform::ReadEnvironmentVariable("RS_DEBUG_ORM_TINT")) {
        std::string mode(v);
        if (mode.empty()) mode = "1";
        result.push_back(L"RS_DEBUG_ORM_TINT=" + std::wstring(mode.begin(), mode.end()));
    }
    return result;
}

} // namespace

// コンパイル済みシェーダをPSO記述へ渡すためのバイトコード変換。
D3D12_SHADER_BYTECODE Bytecode(const ComPtr<IDxcBlob>& blob) {
    return {blob->GetBufferPointer(), blob->GetBufferSize()};
}

// メッシュ描画に使う全シェーダをDXCでコンパイルする。
bool CompileMeshShaders(MeshShaderBlobs& out) {
    // DXC(IDxcCompiler3)でSM6.0としてコンパイルする(HLSL 2021 と共通の bindless 配列を使う)。
    DxcShaderCompiler dxc;
    if (!dxc.Init()) return false;

    const std::vector<std::wstring> defines = BuildDefineArguments();
    auto compileHlsl = [&](LPCWSTR entry, LPCWSTR target, ComPtr<IDxcBlob>& blob) -> bool {
        return dxc.Compile(kMeshShader, entry, target, defines, "mesh", blob);
    };

    ComPtr<IDxcBlob> vsInstancedMultiPart, ps, psCutoutDepthOnly, psTrans, psTransWboit,
        psSolidFade, psHighlight, vsVelocity, psVelocity, psVelocityAlphaTest;
    if (!compileHlsl(L"VSMainInstancedMultiPart", L"vs_6_0", vsInstancedMultiPart)) {
        spdlog::error("[mesh] VSMainInstancedMultiPart compile failed");
        return false;
    }
    if (!compileHlsl(L"PSMain", L"ps_6_0", ps)) {
        spdlog::error("[mesh] PS compile failed");
        return false;
    }
    if (!compileHlsl(L"PSMainCutoutDepthOnly", L"ps_6_0", psCutoutDepthOnly)) {
        spdlog::error("[mesh] PSMainCutoutDepthOnly compile failed");
        return false;
    }
    // 半透明 PS（ソート版）と WBOIT 版（2RT出力）をそれぞれコンパイルする。
    if (!compileHlsl(L"PSMainTrans", L"ps_6_0", psTrans)) {
        spdlog::error("[mesh] PSMainTrans compile failed");
        return false;
    }
    if (!compileHlsl(L"PSMainTransWBOIT", L"ps_6_0", psTransWboit)) {
        spdlog::error("[mesh] PSMainTransWBOIT compile failed");
        return false;
    }
    if (!compileHlsl(L"PSMainSolidFade", L"ps_6_0", psSolidFade)) {
        spdlog::error("[mesh] PSMainSolidFade compile failed");
        return false;
    }
    if (!compileHlsl(L"PSMainHighlight", L"ps_6_0", psHighlight)) {
        spdlog::error("[mesh] PSMainHighlight compile failed");
        return false;
    }
    if (!compileHlsl(L"VSMainVelocity", L"vs_6_0", vsVelocity) ||
        !compileHlsl(L"PSVelocity", L"ps_6_0", psVelocity) ||
        !compileHlsl(L"PSVelocityAlphaTest", L"ps_6_0", psVelocityAlphaTest)) {
        spdlog::error("[mesh] velocity shader compile failed");
        return false;
    }
    out = {vsInstancedMultiPart, vsInstancedMultiPart, vsInstancedMultiPart, ps,
           psCutoutDepthOnly, psTrans, psTransWboit, psSolidFade, psHighlight,
           vsVelocity, psVelocity, psVelocityAlphaTest};
    return true;
}

} // namespace rs::render

