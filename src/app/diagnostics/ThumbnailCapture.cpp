//
// ThumbnailCapture.cpp - ThumbnailCapture.h の実装
//
#include "ThumbnailCapture.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <chrono>
#include <filesystem>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"
#include "../../plugins/load/PluginHeaderRead.h"
#include "../../render/pipeline/MeshPipelineSharedState.h"
#include "../../render/postprocess/PostProcessFormats.h"
#include "../../render/postprocess/TonemapMath.h"
#include "../../render/postprocess/TonemapPass.h"
#include "../gpu/GpuContext.h"
#include "../plugin_browser/PluginImageWorker.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/RailTieHeights.h"
#include "BackBufferCapturePng.h"
#include "HeadlessDevice.h"

namespace rs::app {
namespace {

using Microsoft::WRL::ComPtr;

// 撮影の結果を待つ上限。大きなプラグイン(テクスチャ多数)でも十分な余裕を持たせる。
constexpr std::chrono::seconds kCaptureTimeout{120};
constexpr std::chrono::milliseconds kPollInterval{10};

// 背景の行ごとの色(その行の左端の画素)から、各チャンネルでこの値より離れた画素を「描かれたもの」と数える。
constexpr int kForegroundChannelThreshold = 6;

// 画面の何割が背景以外か(各行の左端の画素=背景の色と比べる)。
double ForegroundRatio(const rs::render::ImageData& image) {
    size_t foreground = 0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        const std::uint8_t* row = image.pixels.data() + static_cast<size_t>(y) * image.width * 4;
        for (std::uint32_t x = 1; x < image.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int diff = static_cast<int>(row[x * 4 + c]) - static_cast<int>(row[c]);
                if (diff > kForegroundChannelThreshold || diff < -kForegroundChannelThreshold) {
                    ++foreground;
                    break;
                }
            }
        }
    }
    return static_cast<double>(foreground) / (static_cast<double>(image.width) * image.height);
}

// フォルダのヘッダから種別を決める。表にない種別(Env等)は nullopt。
std::optional<plugin_browser::BrowserKind> ResolveKind(const std::string& folderUtf8) {
    const rs::legacy_plugin::PluginHeaderReadResult header =
        rs::legacy_plugin::ReadPluginHeader(rs::platform_text::Utf8ToWide(folderUtf8));
    if (!header.ok) return std::nullopt;
    return plugin_browser::BrowserKindFromPluginType(header.header.type);
}

// ワーカーへ1件だけ出して、結果が出るまで待つ。タイムアウトは false。
bool RequestAndWait(plugin_browser::PluginImageWorker& worker, const plugin_browser::PluginImageRequest& request,
                    plugin_browser::PluginImageResult& result, int priority = plugin_browser::kThumbnailPriority) {
    worker.Post(request, priority);
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kCaptureTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<plugin_browser::PluginImageResult> results = worker.Drain(1);
        if (!results.empty()) {
            result = std::move(results.front());
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return false;
}

// 撮影に使う専用のD3D12デバイスと、本編と同じ共有PSO・トーンマップ(--setting tonemap=off でパススルー)。
struct CaptureGpu {
    ComPtr<ID3D12Device> device;
    rs::render::MeshPipelineSharedState shared;
    rs::render::TonemapPass tonemap;

    bool Init() {
        device = CreateHeadlessDevice();
        if (!device) {
            spdlog::error("[thumbnail] D3D12デバイスを作れませんでした");
            return false;
        }
        if (!shared.Init(device.Get(), rs::render::kSceneHdrFormat)) {
            spdlog::error("[thumbnail] 共有のPSOを作れませんでした");
            return false;
        }
        if (!tonemap.Init(rs::rhi::Device(device.Get()), rs::render::LoadTonemapParams())) {
            spdlog::error("[thumbnail] トーンマップのPSOを作れませんでした");
            return false;
        }
        return true;
    }
};

// 統計を出して、pngPathUtf8 が空でなければ保存する。保存に失敗したら false。
bool ReportAndSave(const rs::render::ImageData& image, int debugMessages, const std::string& pngPathUtf8) {
    spdlog::info("[thumbnail] 撮影しました {}x{} 背景以外={:.1f}% D3D12の警告/エラー={}件", image.width,
                 image.height, ForegroundRatio(image) * 100.0, debugMessages);
    if (pngPathUtf8.empty()) return true;
    std::vector<std::string> log;
    const bool saved = SaveBackBufferPng(pngPathUtf8, image.pixels, image.width, image.height, image.width * 4, log);
    for (const std::string& line : log) spdlog::info("{}", line);
    return saved;
}

// "A;B;C" をフォルダの並びに分ける(空の要素は捨てる)。
std::vector<std::string> SplitFolders(const std::string& joined) {
    std::vector<std::string> folders;
    size_t begin = 0;
    while (begin <= joined.size()) {
        size_t end = joined.find(';', begin);
        if (end == std::string::npos) end = joined.size();
        if (end > begin) folders.push_back(joined.substr(begin, end - begin));
        begin = end + 1;
    }
    return folders;
}

} // namespace

int RunThumbnailCapture(const std::string& folderUtf8, const std::string& pngPathUtf8) {
    const std::optional<plugin_browser::BrowserKind> kind = ResolveKind(folderUtf8);
    if (!kind.has_value()) {
        spdlog::error("[thumbnail] プラグインの種別を読めませんでした(または対象外の種別): {}", folderUtf8);
        return 1;
    }
    CaptureGpu gpu;
    if (!gpu.Init()) return 1;

    plugin_browser::PluginImageResult result;
    plugin_browser::PluginImageResult second;
    bool received = false;
    {
        // ワーカーを作って壊すところまで通す(Init・撮影・破棄がすべてワーカースレッドで走る)。
        plugin_browser::PluginImageWorker worker(rs::rhi::Device(gpu.device.Get()), &gpu.shared, &gpu.tonemap);
        plugin_browser::PluginImageRequest request;
        request.id = 1;
        request.generation = 1;
        request.kind = plugin_browser::ImageKind::Thumbnail;
        request.folderUtf8 = folderUtf8;
        request.browserKind = *kind;
        received = RequestAndWait(worker, request, result);
        // 同じ受付をもう1回出して、1枚ごとの後片付け(メッシュ・テクスチャを手放す)が
        // 次の撮影に影響しないこと(同じ画像になること)を確かめる。
        request.id = 2;
        received = received && RequestAndWait(worker, request, second);
    }
    const int debugMessages = ReportDebugMessages(gpu.device.Get(), "thumbnail");

    if (!received || !result.ok || !second.ok) {
        spdlog::error("[thumbnail] 撮影に失敗しました: {}", folderUtf8);
        return 1;
    }
    if (result.image.pixels != second.image.pixels) {
        spdlog::error("[thumbnail] 同じプラグインを続けて撮ると画像が変わります: {}", folderUtf8);
        return 1;
    }
    return ReportAndSave(result.image, debugMessages, pngPathUtf8) ? 0 : 1;
}

int RunLayingSetPreviewCapture(const std::string& foldersUtf8, const std::string& pngPathUtf8) {
    const std::vector<std::string> folders = SplitFolders(foldersUtf8);
    if (folders.empty()) {
        spdlog::error("[laying-set] フォルダが指定されていません");
        return 1;
    }
    // 敷設と同じ経路で高さを求める(PluginSelector が読んだ Rail・Tie の最大高さ)。
    PluginSelector selector;
    for (const std::string& folder : folders) {
        std::string error;
        if (!selector.AddPath(folder, error)) spdlog::warn("[laying-set] {} を読めませんでした: {}", folder, error);
    }
    CaptureGpu gpu;
    if (!gpu.Init()) return 1;

    plugin_browser::PluginImageResult result;
    bool received = false;
    {
        plugin_browser::PluginImageWorker worker(rs::rhi::Device(gpu.device.Get()), &gpu.shared, &gpu.tonemap);
        plugin_browser::PluginImageRequest request;
        request.kind = plugin_browser::ImageKind::LayingSet;
        request.layingFolders = folders;
        request.layingHeights = ComputeRailTieHeights(folders, selector);
        request.layingSerial = 1;
        spdlog::info("[laying-set] Rail高さ={} Tie高さ={}", request.layingHeights.maxRailHeight,
                     request.layingHeights.maxTieHeight);
        received = RequestAndWait(worker, request, result, plugin_browser::kLayingSetPreviewPriority);
    }
    const int debugMessages = ReportDebugMessages(gpu.device.Get(), "laying-set");
    if (!received || !result.ok) {
        spdlog::error("[laying-set] 撮影に失敗しました: {}", foldersUtf8);
        return 1;
    }
    return ReportAndSave(result.image, debugMessages, pngPathUtf8) ? 0 : 1;
}

int RunTemplateThumbnailCapture(const std::string& templatePathUtf8, const std::string& pngPathUtf8) {
    // テンプレートの種類は、置き場所のフォルダ名で決める(RailwayPluginSet か、それ以外=編成)。
    const std::filesystem::path path(rs::platform_text::Utf8ToWide(templatePathUtf8));
    const bool rail = path.parent_path().filename() == L"RailwayPluginSet";
    CaptureGpu gpu;
    if (!gpu.Init()) return 1;

    plugin_browser::PluginImageResult result;
    bool received = false;
    {
        plugin_browser::PluginImageWorker worker(rs::rhi::Device(gpu.device.Get()), &gpu.shared, &gpu.tonemap);
        plugin_browser::PluginImageRequest request;
        request.kind = rail ? plugin_browser::ImageKind::RailTemplate : plugin_browser::ImageKind::TrainTemplate;
        request.templatePathUtf8 = templatePathUtf8;
        request.templateKey = 1;
        received = RequestAndWait(worker, request, result, plugin_browser::kTemplateImagePriority);
    }
    const int debugMessages = ReportDebugMessages(gpu.device.Get(), "template-thumbnail");
    if (!received || !result.ok) {
        spdlog::error("[template-thumbnail] 撮影に失敗しました: {}", templatePathUtf8);
        return 1;
    }
    return ReportAndSave(result.image, debugMessages, pngPathUtf8) ? 0 : 1;
}

} // namespace rs::app
