#include "LayoutThumbnail.h"

#include <objbase.h>

#include <DirectXTex.h>
#include <wincodec.h>

#include <algorithm>

#include "RslThumbnailRead.h"
#include "../../platform/text/Base64.h"
#include "../../platform/text/Encoding.h"

namespace rs::app {
namespace {

constexpr std::size_t kThumbnailWidth = 320;
constexpr std::size_t kThumbnailHeight = 180;
constexpr float kJpegQuality = 0.75f;

// JPEGの品質をWICのエンコーダへ渡す。
void SetJpegQuality(IPropertyBag2* properties) {
    PROPBAG2 option = {};
    option.pstrName = const_cast<wchar_t*>(L"ImageQuality");
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_R4;
    value.fltVal = kJpegQuality;
    properties->Write(1, &option, &value);
    VariantClear(&value);
}

} // namespace

std::string EncodeLayoutThumbnail(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t rowPitch) {
    if (pixels.empty() || width == 0 || height == 0) return {};
    // 中央の16:9だけを使う(ウィンドウの縦横比によらず、カードの枠に合わせる)。
    const std::size_t cropWidth = std::min<std::size_t>(width, std::size_t{height} * kThumbnailWidth / kThumbnailHeight);
    const std::size_t cropHeight = std::min<std::size_t>(height, cropWidth * kThumbnailHeight / kThumbnailWidth);
    if (cropWidth == 0 || cropHeight == 0) return {};
    const std::size_t offsetX = (width - cropWidth) / 2;
    const std::size_t offsetY = (height - cropHeight) / 2;

    DirectX::Image source{};
    source.width = cropWidth;
    source.height = cropHeight;
    source.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    source.rowPitch = rowPitch;
    source.slicePitch = std::size_t{rowPitch} * cropHeight;
    source.pixels = const_cast<std::uint8_t*>(pixels.data()) + offsetY * rowPitch + offsetX * 4;

    DirectX::ScratchImage resized;
    if (FAILED(DirectX::Resize(source, kThumbnailWidth, kThumbnailHeight, DirectX::TEX_FILTER_LINEAR, resized))) {
        return {};
    }

    DirectX::Blob blob;
    if (FAILED(DirectX::SaveToWICMemory(*resized.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
                                        GUID_ContainerFormatJpeg, blob, nullptr, SetJpegQuality))) {
        return {};
    }
    return rs::platform_text::EncodeBase64(blob.GetConstBufferPointer(), blob.GetBufferSize());
}

void LayoutThumbnailLoader::Post(LayoutThumbnailRequest request) {
    if (worker_ == nullptr) {
        worker_ = std::make_unique<Worker>(
            [](const LayoutThumbnailRequest& item, std::stop_token) {
                LayoutThumbnailResult result;
                result.generation = item.generation;
                result.pathUtf8 = item.pathUtf8;
                const std::string base64 = ReadRslThumbnailBase64(
                    std::filesystem::path(rs::platform_text::Utf8ToWide(item.pathUtf8)));
                const std::vector<std::uint8_t> bytes = rs::platform_text::DecodeBase64(base64);
                if (!bytes.empty()) result.image = rs::render::DecodeImageBytesRGBA(bytes.data(), bytes.size());
                return result;
            });
    }
    worker_->Post(std::move(request), 0);
}

void LayoutThumbnailLoader::ClearPending() {
    if (worker_ != nullptr) worker_->ClearPending();
}

std::vector<LayoutThumbnailResult> LayoutThumbnailLoader::Drain(std::size_t maxCount) {
    return worker_ != nullptr ? worker_->Drain(maxCount) : std::vector<LayoutThumbnailResult>{};
}

} // namespace rs::app
