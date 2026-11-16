#include "PluginImageWorker.h"

#include <objbase.h>

#include <cstring>
#include <utility>

#include <spdlog/spdlog.h>

#include "../gpu/App.h"
#include "IconRectCrop.h"
#include "PluginThumbnailRecipe.h"
#include "TemplateThumbnailRecipe.h"

namespace rs::app::plugin_browser {
namespace {

// サムネイル撮影用のSRVレンジの配分。本編のシーン常駐スロット1つぶんと同じ大きさ・同じ内訳で、
// 値はApp.hの定数から取る(render層は app 層の定数を参照しないため、ここで詰めて渡す)。
rs::render::ThumbnailRendererConfig MakeThumbnailConfig(std::uint32_t width, std::uint32_t height) {
    rs::render::ThumbnailRendererConfig config;
    config.width = width;
    config.height = height;
    config.srvStride = kSceneEntitySrvStride;
    config.maxTextures = kSceneEntityMaxTextures;
    config.transBufSrvCount = kTransBufSrvCount;
    config.partWorldSrvCount = kPartWorldSrvCount;
    config.maxParts = kSceneEntityMaxParts;
    return config;
}

PluginImageResult MakeResult(const PluginImageRequest& request, ImageKind kind) {
    PluginImageResult result;
    result.id = request.id;
    result.generation = request.generation;
    result.kind = kind;
    result.view = request.view;
    result.layingSerial = request.layingSerial;
    result.templateKey = request.templateKey;
    result.ok = false;
    return result;
}

PluginImageResult DecodeIcon(const PluginImageRequest& request) {
    PluginImageResult result = MakeResult(request, ImageKind::Icon);
    if (!request.iconPathUtf8.empty()) {
        result.image = CropIconRect(rs::render::LoadImageRGBA(request.iconPathUtf8), request.iconRect);
        result.ok = result.image.ok;
    }
    if (!result.ok) result.image = rs::render::ImageData{};
    return result;
}

// 撮影器へメッシュとカメラを渡して撮る。撮れなかったら画像を空にして ok=false のまま。
void Capture(rs::render::ThumbnailRenderer& renderer, const ThumbnailRecipeResult& recipe,
             PluginImageResult& result) {
    rs::render::ThumbnailScene scene;
    scene.mesh = &recipe.mesh;
    std::memcpy(scene.viewProj, recipe.viewProj, sizeof(scene.viewProj));
    std::memcpy(scene.cameraPos, recipe.cameraPos, sizeof(scene.cameraPos));
    result.ok = renderer.Render(scene, result.image);
    if (!result.ok) result.image = rs::render::ImageData{};
}

} // namespace

PluginImageWorker::PluginImageWorker(rs::rhi::Device device, const rs::render::MeshPipelineSharedState* shared,
                                     const rs::render::TonemapPass* tonemap)
    : device_(device),
      shared_(shared),
      tonemap_(tonemap),
      worker_(
          [this](const PluginImageRequest& request, std::stop_token stopToken) {
              switch (request.kind) {
                  case ImageKind::Icon:
                      return DecodeIcon(request);
                  case ImageKind::LayingSet:
                      return RenderLayingSet(request, stopToken);
                  case ImageKind::TrainTemplate:
                  case ImageKind::RailTemplate:
                      return RenderTemplate(request, stopToken);
                  case ImageKind::Thumbnail:
                      break;
              }
              return RenderThumbnail(request, stopToken);
          },
          [this] {
              // WICを使うので、このスレッドもCOMを初期化する(メインと同じMTA)。
              comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
              InitThumbnailRenderer();
          },
          [this] {
              // GPU資源は作ったスレッドで壊す(デストラクタがGPUの完了を待つ)。
              thumbnailRenderers_.clear();
              if (comInitialized_) CoUninitialize();
          }) {}

void PluginImageWorker::InitThumbnailRenderer() {
    // 標準の解像度だけ先に作り、初期化できない環境(デバイスなし等)を早めに知らせる。
    const ThumbnailViewTraits& standard = GetThumbnailView(ThumbnailView::Standard);
    if (RendererFor(standard.width, standard.height) != nullptr) {
        spdlog::debug("[plugin_browser] サムネイルの撮影を初期化しました");
    }
}

rs::render::ThumbnailRenderer* PluginImageWorker::RendererFor(std::uint32_t width, std::uint32_t height) {
    for (const SizedRenderer& entry : thumbnailRenderers_) {
        if (entry.width == width && entry.height == height) return entry.renderer.get();
    }
    SizedRenderer entry;
    entry.width = width;
    entry.height = height;
    if (device_.Valid() && shared_ != nullptr && tonemap_ != nullptr) {
        std::unique_ptr<rs::render::ThumbnailRenderer> renderer = std::make_unique<rs::render::ThumbnailRenderer>();
        if (renderer->Init(device_, *shared_, *tonemap_, MakeThumbnailConfig(width, height))) {
            entry.renderer = std::move(renderer);
        } else {
            spdlog::warn("[plugin_browser] サムネイルの撮影({}x{})を初期化できませんでした。カードはアイコンのままです",
                         width, height);
        }
    }
    thumbnailRenderers_.push_back(std::move(entry));
    return thumbnailRenderers_.back().renderer.get();
}

PluginImageResult PluginImageWorker::RenderThumbnail(const PluginImageRequest& request, std::stop_token stopToken) {
    PluginImageResult result = MakeResult(request, ImageKind::Thumbnail);
    if (stopToken.stop_requested()) return result;
    const ThumbnailViewTraits& viewTraits = GetThumbnailView(request.view);
    rs::render::ThumbnailRenderer* renderer = RendererFor(viewTraits.width, viewTraits.height);
    if (renderer == nullptr) return result;

    ThumbnailRecipeResult recipe = BuildThumbnailRecipe(request.folderUtf8, request.browserKind, request.view,
                                                        viewTraits.width, viewTraits.height);
    if (stopToken.stop_requested()) return result;
    if (recipe.noModel) {
        // 意図してモデルが無いプラグイン(定義は読めている)。背景だけの画像を成功として返す。
        result.ok = renderer->RenderBackground(result.image);
        if (!result.ok) result.image = rs::render::ImageData{};
        return result;
    }
    if (!recipe.ok) return result;

    Capture(*renderer, recipe, result);
    return result;
}

PluginImageResult PluginImageWorker::RenderLayingSet(const PluginImageRequest& request, std::stop_token stopToken) {
    PluginImageResult result = MakeResult(request, ImageKind::LayingSet);
    if (stopToken.stop_requested()) return result;
    const ThumbnailViewTraits& viewTraits = GetThumbnailView(ThumbnailView::LayingSet);
    rs::render::ThumbnailRenderer* renderer = RendererFor(viewTraits.width, viewTraits.height);
    if (renderer == nullptr) return result;

    const ThumbnailRecipeResult recipe = BuildLayingSetRecipe(request.layingFolders, request.layingHeights,
                                                              ThumbnailView::LayingSet, viewTraits.width, viewTraits.height, stopToken);
    if (stopToken.stop_requested() || !recipe.ok) return result;
    Capture(*renderer, recipe, result);
    return result;
}

PluginImageResult PluginImageWorker::RenderTemplate(const PluginImageRequest& request, std::stop_token stopToken) {
    PluginImageResult result = MakeResult(request, request.kind);
    if (stopToken.stop_requested()) return result;
    const bool train = request.kind == ImageKind::TrainTemplate;
    const ThumbnailViewTraits& viewTraits =
        GetThumbnailView(train ? ThumbnailView::TrainTemplate : ThumbnailView::RailTemplate);
    rs::render::ThumbnailRenderer* renderer = RendererFor(viewTraits.width, viewTraits.height);
    if (renderer == nullptr) return result;

    const ThumbnailRecipeResult recipe =
        train ? BuildTrainTemplateRecipe(request.templatePathUtf8, request.templateRootUtf8, viewTraits.width,
                                         viewTraits.height, stopToken)
              : BuildRailTemplateRecipe(request.templatePathUtf8, request.templateRootUtf8, viewTraits.width,
                                        viewTraits.height, stopToken);
    if (stopToken.stop_requested() || !recipe.ok) return result;
    Capture(*renderer, recipe, result);
    return result;
}

void PluginImageWorker::Post(PluginImageRequest request, int priority) {
    worker_.Post(std::move(request), priority);
}

void PluginImageWorker::ClearPending() {
    worker_.ClearPendingIf(IsBrowserImageRequest);
}

void PluginImageWorker::ClearPendingLayingSet() {
    worker_.ClearPendingIf(IsLayingSetRequest);
}

std::vector<PluginImageResult> PluginImageWorker::Drain(std::size_t maxCount) {
    return worker_.Drain(maxCount);
}

} // namespace rs::app::plugin_browser
