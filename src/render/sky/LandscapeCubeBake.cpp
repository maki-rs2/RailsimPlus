#include <d3d12.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstring>
#include <iterator>
#include <string>

#include <spdlog/spdlog.h>

#include "AtmosphereGpuUtil.h"
#include "LandscapeCubeImpl.h"
#include "LandscapeMesh.h"
#include "LandscapeShader.h"

namespace rs::render {

using namespace landscape_cube;

bool LandscapeCube::Impl::CreatePipelines() {
    ID3D12Device* native = device.Get();
    rootSigMesh = CreateAtmosphereRootSignature(native, 1, kFaceConstantDwords, 0, true);
    rootSigDownsample = CreateAtmosphereRootSignature(native, 1, kDownsampleConstantDwords);
    if (!rootSigMesh || !rootSigDownsample) {
        spdlog::warn("[landscape] ルート署名を作れません");
        return false;
    }

    const std::string meshSource = kLandscapeBakeMeshShader;
    const std::string downSource = kLandscapeBakeDownsampleShader;
    ComPtr<ID3DBlob> meshVs = CompileAtmosphereShader(meshSource, "MeshVS", "vs_5_0", false);
    ComPtr<ID3DBlob> meshPs = CompileAtmosphereShader(meshSource, "MeshPS", "ps_5_0", false);
    ComPtr<ID3DBlob> fullVs = CompileAtmosphereShader(downSource, "FullscreenVS", "vs_5_0", false);
    ComPtr<ID3DBlob> downPs = CompileAtmosphereShader(downSource, "DownsamplePS", "ps_5_0", false);
    if (!meshVs || !meshPs || !fullVs || !downPs) {
        spdlog::warn("[landscape] 焼き込みのシェーダをコンパイルできません");
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(LandscapeVertex, position),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(LandscapeVertex, uv),
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    // 深度なし・ブレンドなし(上書き)・カリングなし。z>=0 のクリップ(w>=near)で背面を落とす。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rootSigMesh.Get();
    pd.VS = {meshVs->GetBufferPointer(), meshVs->GetBufferSize()};
    pd.PS = {meshPs->GetBufferPointer(), meshPs->GetBufferSize()};
    pd.InputLayout = {layout, static_cast<UINT>(std::size(layout))};
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = kSceneHdrFormat;
    pd.SampleDesc.Count = 1;
    const HRESULT meshResult = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoMesh));
    if (FAILED(meshResult)) {
        spdlog::warn("[landscape] メッシュ描画のPSOを作れません (hr=0x{:08x})", static_cast<unsigned>(meshResult));
        return false;
    }

    psoDownsample = CreateAtmosphereFullscreenPso(native, rootSigDownsample.Get(), fullVs.Get(), downPs.Get(),
                                                  kSceneHdrFormat);
    return psoDownsample != nullptr;
}

void LandscapeCube::Impl::RecordBake(ID3D12GraphicsCommandList* list) const {
    ID3D12DescriptorHeap* heaps[] = {privateSrvHeap.Get()};
    list->SetDescriptorHeaps(1, heaps);

    const D3D12_RESOURCE_BARRIER toRenderTarget =
        TransitionBarrier(cube.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    list->ResourceBarrier(1, &toRenderTarget);

    // ミップ0: 帯のメッシュを6方向に描く(クリアは透明)。
    list->SetGraphicsRootSignature(rootSigMesh.Get());
    list->SetPipelineState(psoMesh.Get());
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vertexView);
    list->IASetIndexBuffer(&indexView);
    list->SetGraphicsRootDescriptorTable(0, PrivateSrvGpu(0));
    const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (std::uint32_t face = 0; face < kFaceCount; ++face) {
        BindFace(list, face, 0);
        list->ClearRenderTargetView(RtvHandle(face, 0), transparent, 0, nullptr);
        const LandscapeFaceBasis& basis = LandscapeCubeFaceBasis(static_cast<int>(face));
        FaceShaderConstants constants{};
        std::memcpy(constants.right, basis.right, sizeof(constants.right));
        std::memcpy(constants.up, basis.up, sizeof(constants.up));
        std::memcpy(constants.forward, basis.forward, sizeof(constants.forward));
        list->SetGraphicsRoot32BitConstants(1, kFaceConstantDwords, &constants, 0);
        list->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
    }

    // ミップ1以降: 1つ上のミップを読んで書く。読む側(ミップ m-1)だけSRV状態へ戻してから描く。
    list->SetGraphicsRootSignature(rootSigDownsample.Get());
    list->SetPipelineState(psoDownsample.Get());
    list->IASetVertexBuffers(0, 0, nullptr);
    list->IASetIndexBuffer(nullptr);
    for (std::uint32_t mip = 1; mip <= kMipCount; ++mip) {
        D3D12_RESOURCE_BARRIER toShaderResource[kFaceCount];
        for (std::uint32_t face = 0; face < kFaceCount; ++face) {
            toShaderResource[face] = TransitionBarrier(cube.Get(), SubresourceIndex(face, mip - 1),
                                                       D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                       D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        list->ResourceBarrier(kFaceCount, toShaderResource);
        if (mip == kMipCount) break; // 最後のミップをSRV状態へ戻して終わり
        list->SetGraphicsRootDescriptorTable(0, PrivateSrvGpu(mip));
        for (std::uint32_t face = 0; face < kFaceCount; ++face) {
            BindFace(list, face, mip);
            const DownsampleShaderConstants constants{face};
            list->SetGraphicsRoot32BitConstants(1, kDownsampleConstantDwords, &constants, 0);
            list->DrawInstanced(3, 1, 0, 0);
        }
    }
}

} // namespace rs::render
