#include "EnvTextureLoader.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <exception>
#include <filesystem>

#include <DirectXTex.h>

#include "../../platform/text/Encoding.h"

namespace rs::render {

namespace {

EnvTextureData Fail(const std::string& message) {
    EnvTextureData data;
    data.error = message;
    return data;
}

// 24bit RGB(D3DFMT_R8G8B8)はDirectXTexがR8G8B8A8_UNORM(αは不透明)へ広げて読む。背景の見た目の画像として
// 使うため、その色はsRGBとして扱う。
bool ToEnvTexelFormat(DXGI_FORMAT format, EnvTexelFormat& out) {
    switch (format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: out = EnvTexelFormat::Rgba8Srgb; return true;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: out = EnvTexelFormat::Rgba16Float; return true;
    case DXGI_FORMAT_R32G32B32A32_FLOAT: out = EnvTexelFormat::Rgba32Float; return true;
    default: return false;
    }
}

// DirectXTexで読み、8bit・16bit浮動小数・32bit浮動小数のRGBAのキューブ・2Dを、行の詰めなしのサブリソース列へ詰める。
EnvTextureData LoadWithDirectXTex(const std::wstring& wpath) {
    DirectX::ScratchImage scratch;
    if (FAILED(DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, scratch))) {
        return Fail("DirectXTex failed to read the DDS (unsupported or corrupt)");
    }
    const DirectX::TexMetadata& meta = scratch.GetMetadata();
    if (meta.dimension == DirectX::TEX_DIMENSION_TEXTURE3D) return Fail("volume DDS is not supported");

    EnvTextureData data;
    if (!ToEnvTexelFormat(meta.format, data.format)) {
        return Fail("DDS pixel format is not supported (expected RGB24/RGBA8, A16B16G16R16F or A32B32G32R32F)");
    }
    data.isCube = (meta.miscFlags & DirectX::TEX_MISC_TEXTURECUBE) != 0;
    data.width = static_cast<std::uint32_t>(meta.width);
    data.height = static_cast<std::uint32_t>(meta.height);
    data.mipLevels = static_cast<std::uint32_t>(meta.mipLevels);
    data.arraySize = static_cast<std::uint32_t>(meta.arraySize);

    const std::size_t bytesPerPixel = DirectX::BitsPerPixel(meta.format) / 8;
    for (std::uint32_t item = 0; item < data.arraySize; ++item) {
        for (std::uint32_t mip = 0; mip < data.mipLevels; ++mip) {
            const DirectX::Image* image = scratch.GetImage(mip, item, 0);
            if (image == nullptr) return Fail("DDS subresource is missing");
            EnvTextureSubresource sub;
            sub.offset = data.bytes.size();
            sub.rowPitch = image->width * bytesPerPixel;
            sub.slicePitch = sub.rowPitch * image->height;
            data.subresources.push_back(sub);
            data.bytes.resize(sub.offset + sub.slicePitch);
            for (std::size_t y = 0; y < image->height; ++y) {
                std::memcpy(data.bytes.data() + sub.offset + y * sub.rowPitch,
                            image->pixels + y * image->rowPitch, sub.rowPitch);
            }
        }
    }
    data.ok = true;
    return data;
}

} // namespace

EnvTextureData LoadEnvTextureDds(const std::string& utf8Path) {
    try {
        const std::wstring wpath = rs::platform_text::Utf8ToWide(utf8Path);
        std::error_code ec;
        if (!std::filesystem::exists(wpath, ec)) return Fail("file not found");

        return LoadWithDirectXTex(wpath);
    } catch (const std::exception& e) {
        return Fail(std::string("exception while reading the DDS: ") + e.what());
    }
}

} // namespace rs::render
