#include "TextureUpload.h"

#include "RenderTuning.h"

#include <cmath>

using Microsoft::WRL::ComPtr;

namespace rs::render {

ComPtr<ID3D12Resource> CreateGpuTexture(
    ID3D12Device* device, DirectX::ResourceUploadBatch& upload,
    const ImageData& imgIn, uint32_t fallbackRGBA, bool isSrgb, bool* hasAlphaOut,
    bool* hasZeroAlphaOut, bool mipIgnoresAlpha, float* semiAlphaFractionOut) {
    // img.ok の通常ケースでは imgIn を参照で使い、全ピクセルのコピーを避ける
    // （527MB 級の連結テクスチャで顕著）。無いときだけ 1x1 fallback を局所生成する。
    ImageData fallback;
    const ImageData* imgPtr = &imgIn;
    if (!imgIn.ok) {
        fallback.width = fallback.height = 1;
        fallback.pixels.assign(4, 0);
        fallback.pixels[0] = uint8_t(fallbackRGBA & 0xFF);
        fallback.pixels[1] = uint8_t((fallbackRGBA >> 8) & 0xFF);
        fallback.pixels[2] = uint8_t((fallbackRGBA >> 16) & 0xFF);
        fallback.pixels[3] = uint8_t((fallbackRGBA >> 24) & 0xFF);
        fallback.ok = true;
        imgPtr = &fallback;
    }
    const ImageData& img = *imgPtr;
    // アルファ走査（アップロード時に1回だけ）:
    //   nonOpaque    = alpha<255 の texel あり（抜き or 半透明）
    //   hasZeroAlpha = alpha==0 の texel あり（穴/透明背景 → 要クリップ）
    //   semiAlphaFraction = 半透明(下限以上、上限未満)のtexelが、alpha>0のtexelに占める割合
    bool nonOpaque = false;
    bool zeroAlpha = false;
    const RenderTuning& tuning = RenderTuning::Embedded();
    const int semiLowByte = static_cast<int>(std::ceil(tuning.semiAlphaLow * 255.0f));
    const int semiHighByte = static_cast<int>(std::ceil(tuning.semiAlphaHigh * 255.0f));
    size_t semiCount = 0;
    size_t visibleCount = 0;
    for (size_t i = 3; i < img.pixels.size(); i += 4) {
        const int a = img.pixels[i];
        if (a < 255) {
            nonOpaque = true;
            if (a == 0) zeroAlpha = true;
        }
        if (a > 0) {
            ++visibleCount;
            if (a >= semiLowByte && a < semiHighByte) ++semiCount;
        }
    }
    if (hasAlphaOut) *hasAlphaOut = nonOpaque;
    if (hasZeroAlphaOut) *hasZeroAlphaOut = zeroAlpha;
    if (semiAlphaFractionOut) {
        *semiAlphaFractionOut =
            visibleCount > 0 ? static_cast<float>(double(semiCount) / double(visibleCount)) : 0.0f;
    }

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td = {};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = img.width;
    td.Height = img.height;
    td.DepthOrArraySize = 1;
    // MipLevels=0 は「最大解像度から1x1までフルチェーンを自動計算」の指定
    // (D3D12の規約)。遠景でのエイリアシング/モアレ対策として、読込んだ
    // ベース画像から GenerateMips でフルミップチェーンを生成する。
    // ただしアルファ抜き/半透明テクスチャ(木の葉のcutout・ガラス調材質等)は
    // 対象外にする: box filterでミップを生成するとα平均が下がり、葉の密集
    // 部分のような疎らな不透明画素が下位ミップで全体的に半透明化して見える
    // (coverage mipmappingの既知の副作用)。カットアウト/ブレンド材質はUV
    // 微分から高いミップが選ばれやすい分だけこの副作用が目立つため、当面は
    // ミップ0のみに留める(不透明のベース/地面/壁面テクスチャのみミップ化)。
    // 法線マップ・ORMはアルファを抜きに使わないので、アルファに関係なくミップ化する(mipIgnoresAlpha)。
    const bool generateMips = mipIgnoresAlpha || !nonOpaque;
    td.MipLevels = generateMips ? 0 : 1;
    td.Format = isSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> tex;
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&tex))))
        return nullptr;

    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = img.pixels.data();
    subresource.RowPitch = LONG_PTR(img.width) * 4;
    subresource.SlicePitch = subresource.RowPitch * img.height;
    upload.Upload(tex.Get(), 0, &subresource, 1);
    upload.Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (generateMips) {
        // ミップ0(読込んだそのまま)から下位ミップをGPU上で生成する。
        // 1x1テクスチャ等 MipLevels==1 になったリソースは内部で早期returnされる。
        upload.GenerateMips(tex.Get());
    }

    return tex;
}

void WriteTextureSrv(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                      ID3D12Resource* texture, UINT slot, bool isSrgb) {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(slot) * srvSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
    sv.Format = isSrgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    // -1(全ビット1)は「リソースが持つ全ミップレベルを使う」の意(D3D12の規約)。
    sv.Texture2D.MipLevels = UINT(-1);
    device->CreateShaderResourceView(texture, &sv, cpu);
}

void WriteStructuredBufferSrv(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvSize,
                              ID3D12Resource* buffer, UINT numElements,
                              UINT structureByteStride, UINT slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = srvHeap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(slot) * srvSize;
    D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Buffer.NumElements = numElements;
    srv.Buffer.StructureByteStride = structureByteStride;
    device->CreateShaderResourceView(buffer, &srv, cpu);
}

} // namespace rs::render
