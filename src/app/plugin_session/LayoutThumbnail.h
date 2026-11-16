//
// LayoutThumbnail.h - レイアウトのサムネイルの作成と、一覧の画面のための読込。
//
// サムネイルは16:9の小さなJPEGをBase64の文字列にして.rslのinfo.thumbnailへ入れる。
//
#pragma once

#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "../../platform/thread/WorkerThread.h"
#include "../../render/resource/TextureLoader.h"

namespace rs::app {

// 読み戻した画面(RGBA8、rowPitch込み)の中央を16:9に切り抜き、縮小してBase64のJPEGにする。失敗したら空。
std::string EncodeLayoutThumbnail(const std::vector<std::uint8_t>& pixels, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t rowPitch);

struct LayoutThumbnailRequest {
    std::uint64_t generation = 0;
    std::string pathUtf8;
};

struct LayoutThumbnailResult {
    std::uint64_t generation = 0;
    std::string pathUtf8;
    rs::render::ImageData image; // 読めない、または撮っていないファイルならok=false
};

// .rslからサムネイルを読むワーカー。最初のPostでスレッドを作る。
class LayoutThumbnailLoader {
public:
    void Post(LayoutThumbnailRequest request);
    // まだ読み始めていない受付を捨てる。
    void ClearPending();
    std::vector<LayoutThumbnailResult> Drain(std::size_t maxCount);

private:
    using Worker = rs::platform_thread::WorkerThread<LayoutThumbnailRequest, LayoutThumbnailResult>;
    std::unique_ptr<Worker> worker_;
};

} // namespace rs::app
