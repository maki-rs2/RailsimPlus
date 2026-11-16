//
// BackBufferCapturePng.cpp - BackBufferCapturePng.h の実装
//
#include "BackBufferCapturePng.h"

#include <DirectXTex.h>
#include <wincodec.h>

#include "../../platform/text/Encoding.h"

namespace rs::app {

bool SaveBackBufferPng(const std::string& pathUtf8, const std::vector<uint8_t>& pixels,
                       UINT width, UINT height, UINT rowPitch, std::vector<std::string>& log) {
    if (pixels.empty() || width == 0 || height == 0) {
        log.push_back("[capture-png] バックバッファが空です");
        return false;
    }
    DirectX::Image image{};
    image.width = width;
    image.height = height;
    // 画素はsRGBのRTV(kBackBufferRtvFormat・tonemapの出力)で書いた値。sRGBとして渡さないと、
    // DirectXTexがPNGにガンマ1.0の印(gAMA)を付け、ビューアーで明るく表示される
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    image.rowPitch = rowPitch;
    image.slicePitch = static_cast<size_t>(rowPitch) * height;
    image.pixels = const_cast<uint8_t*>(pixels.data());

    const std::wstring pathWide = rs::platform_text::Utf8ToWide(pathUtf8);
    const HRESULT hr = DirectX::SaveToWICFile(image, DirectX::WIC_FLAGS_NONE,
                                              GUID_ContainerFormatPng, pathWide.c_str());
    if (FAILED(hr)) {
        log.push_back("[capture-png] PNG保存失敗: " + pathUtf8);
        return false;
    }
    log.push_back("[capture-png] 保存しました: " + pathUtf8);
    return true;
}

} // namespace rs::app
