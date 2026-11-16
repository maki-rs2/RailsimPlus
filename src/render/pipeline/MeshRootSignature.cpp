// MeshRootSignature.cpp - MeshPipelineSharedState のルートシグネチャ生成
// MeshPipelineSharedState.cpp から分離。

#include "MeshPipelineSharedState.h"

#include <spdlog/spdlog.h>

using Microsoft::WRL::ComPtr;

namespace rs::render {

bool MeshPipelineSharedState::CreateMeshRootSignature(ID3D12Device* device) {
    // ルートシグネチャ: b0 = CBV (root descriptor), s0 = static sampler。
    // Bindless方式: base/normal/orm固定3レンジやgTexArr全域レンジは持たない。シェーダは
    // D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED(SM6.6)により
    // 現在バインド中のsrvHeap_全体をResourceDescriptorHeap[]で直接参照する
    // (テクスチャ添字はMeshConstants.texIndex/MatData.texへCPU側が絶対値で
    // 書き込む。AbsoluteTexIndex()参照)。

    // 半透明パス用 SRV レンジ: param1 = space1 t0-t1（gTriMat / gMats）—
    // root SRV でも表現できるが構造化バッファ2本をまとめて張るためテーブルにする。
    D3D12_DESCRIPTOR_RANGE transBufRange = {};
    transBufRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    transBufRange.NumDescriptors = 2; // t0=gTriMat, t1=gMats
    transBufRange.BaseShaderRegister = 0;
    transBufRange.RegisterSpace = 1;
    transBufRange.OffsetInDescriptorsFromTableStart = 0;

    // param2 = space3 t0（gPartWorld）— 頂点シェーダが partIndex で引く
    // パーツ単位ワールド行列。フレームごとに1枚を SetGraphicsRootDescriptorTable
    // で切り替える。
    D3D12_DESCRIPTOR_RANGE partWorldRange = {};
    partWorldRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    partWorldRange.NumDescriptors = 1;
    partWorldRange.BaseShaderRegister = 0;
    partWorldRange.RegisterSpace = 3;
    partWorldRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_DESCRIPTOR_RANGE overrideRange = {};
    overrideRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    overrideRange.NumDescriptors = 2;
    overrideRange.BaseShaderRegister = 0;
    overrideRange.RegisterSpace = 4;
    overrideRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER params[6] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].Descriptor.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &transBufRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &partWorldRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &overrideRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // b1 = 影定数(フレームに1回だけ書く。b0の描画ごとの定数とは分ける)。
    params[kShadowConstantsRootParam].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kShadowConstantsRootParam].Descriptor.ShaderRegister = 1;
    params[kShadowConstantsRootParam].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // b2 = 視点ごとの定数(ViewConstants。viewProj・cameraPos・APのカメラの基底)。視点ごとに1枠で、頂点・ピクセルの両方が読む。
    params[kViewConstantsRootParam].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kViewConstantsRootParam].Descriptor.ShaderRegister = 2;
    params[kViewConstantsRootParam].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_STATIC_SAMPLER_DESC samp = {};
    samp.Filter = D3D12_FILTER_ANISOTROPIC;
    samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samp.MaxAnisotropy = 8;
    samp.MaxLOD = D3D12_FLOAT32_MAX;
    samp.ShaderRegister = 0;
    samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: IBLのキューブ・BRDF LUT・透過率LUT用のリニア・クランプ。s0はWRAPなので、
    // LUTの端が反対側の値とにじむ。
    D3D12_STATIC_SAMPLER_DESC iblSamp = {};
    iblSamp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    iblSamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    iblSamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    iblSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    iblSamp.MaxLOD = D3D12_FLOAT32_MAX;
    iblSamp.ShaderRegister = 1;
    iblSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s2: シャドウマップの比較サンプラ(LESS_EQUAL、2x2のハードウェアPCF)。マップの外は日なた側(1.0)。
    D3D12_STATIC_SAMPLER_DESC shadowSamp = {};
    shadowSamp.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    shadowSamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    shadowSamp.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    shadowSamp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    shadowSamp.MaxLOD = D3D12_FLOAT32_MAX;
    shadowSamp.ShaderRegister = 2;
    shadowSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s3: フィルタ・ミップなし(最近傍、最上位ミップ固定)。AlphaZeroTest のフィルタ・ミップを切る方式の材質が使う。
    D3D12_STATIC_SAMPLER_DESC pointSamp = {};
    pointSamp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    pointSamp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointSamp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointSamp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    pointSamp.MaxLOD = 0.0f;
    pointSamp.ShaderRegister = 3;
    pointSamp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[4] = {samp, iblSamp, shadowSamp, pointSamp};

    D3D12_ROOT_SIGNATURE_DESC rs = {};
    rs.NumParameters = 6;
    rs.pParameters = params;
    rs.NumStaticSamplers = 4;
    rs.pStaticSamplers = samplers;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
               D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

    ComPtr<ID3DBlob> sig, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig,
                                           &err))) {
        if (err) spdlog::error("[mesh] rootsig: {}",
                               (const char*)err->GetBufferPointer());
        return false;
    }
    if (FAILED(device->CreateRootSignature(0, sig->GetBufferPointer(),
                                           sig->GetBufferSize(),
                                           IID_PPV_ARGS(&rootSig_))))
        return false;
    return true;
}

} // namespace rs::render

