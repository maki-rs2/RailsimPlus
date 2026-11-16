// MeshShaderCompile.h - MeshPipelineSharedState のシェーダコンパイルの宣言
// MeshPipelineSharedState.cpp から分離。

#pragma once

#include <d3d12.h>
#include <dxcapi.h>
#include <wrl/client.h>

namespace rs::render {

// PSO記述が参照するシェーダバイトコードの実体。全PSOの生成が終わるまで
// 生存させる必要がある(記述はポインタで参照するだけ)。
struct MeshShaderBlobs {
    Microsoft::WRL::ComPtr<IDxcBlob> vs;
    Microsoft::WRL::ComPtr<IDxcBlob> vsInstanced;
    Microsoft::WRL::ComPtr<IDxcBlob> vsInstancedMultiPart;
    Microsoft::WRL::ComPtr<IDxcBlob> ps;
    Microsoft::WRL::ComPtr<IDxcBlob> psCutoutDepthOnly;
    Microsoft::WRL::ComPtr<IDxcBlob> psTrans;
    Microsoft::WRL::ComPtr<IDxcBlob> psTransWboit;
    Microsoft::WRL::ComPtr<IDxcBlob> psSolidFade;
    Microsoft::WRL::ComPtr<IDxcBlob> psHighlight;
    Microsoft::WRL::ComPtr<IDxcBlob> vsVelocity;
    Microsoft::WRL::ComPtr<IDxcBlob> psVelocity;
    Microsoft::WRL::ComPtr<IDxcBlob> psVelocityAlphaTest;
};

bool CompileMeshShaders(MeshShaderBlobs& out);
D3D12_SHADER_BYTECODE Bytecode(const Microsoft::WRL::ComPtr<IDxcBlob>& blob);

} // namespace rs::render

