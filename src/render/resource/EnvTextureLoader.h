//
// EnvTextureLoader.h - Envプラグインの焼き済みIBL用DDSをメモリ上へ読む
//
// D3D9時代のDDS(キューブ・HDR・24bit RGB)を、GPUへ上げられる形のメモリ上データにする。
// GPUへのアップロードはここでは行わない(DX12型・DXGI型も公開しない)。
// 出力はD3D12のサブリソース順(配列=面が外側、ミップが内側)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rs::render {

enum class EnvTexelFormat {
    Rgba8Srgb,     // 8bit RGBA。色はsRGBとして扱う
    Rgba16Float,   // 16bit浮動小数 RGBA
    Rgba32Float,   // 32bit浮動小数 RGBA
};

// bytes内の1サブリソース(1面の1ミップ)の位置。rowPitchは行の詰め(アラインなし)のバイト数。
struct EnvTextureSubresource {
    std::size_t offset = 0;
    std::size_t rowPitch = 0;
    std::size_t slicePitch = 0;
};

struct EnvTextureData {
    bool        ok = false;
    std::string error;                   // ok=falseのときの理由
    bool        isCube = false;          // キューブ(6面)か
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t mipLevels = 0;
    std::uint32_t arraySize = 0;         // キューブは6
    EnvTexelFormat format = EnvTexelFormat::Rgba8Srgb;
    std::vector<EnvTextureSubresource> subresources;  // 添字 = 面 * mipLevels + ミップ
    std::vector<std::uint8_t> bytes;
};

// UTF-8パスのDDSを読む。対応形式は24bit RGB(DirectXTexがRGBA8へ展開)・A16B16G16R16F・A32B32G32R32F。
// 未対応の形式・壊れたファイル・存在しないパスはok=falseとerrorで返し、例外は投げない。
EnvTextureData LoadEnvTextureDds(const std::string& utf8Path);

} // namespace rs::render
