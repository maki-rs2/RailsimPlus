//
// PluginImageWorker.h - カードの画像(アイコンのデコード・サムネイルの撮影)を作るワーカー
//
// 専用スレッド1本で受付を優先度順に処理し、結果を
// メインスレッドがDrainで受け取る。アイコンはワーカーでデコードするだけ。サムネイルは
// ワーカースレッドで PluginThumbnailRecipe+ThumbnailRenderer を呼んで撮る。
// ThumbnailRenderer は解像度ごとに、その解像度で初めて撮るときに作り、終了時に壊す(GPU資源の作成と破棄を
// ワーカーのスレッドに閉じる)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

#include "../../platform/thread/WorkerThread.h"
#include "../../render/resource/TextureLoader.h"
#include "../../render/rhi/Device.h"
#include "../../render/thumbnail/ThumbnailRenderer.h"
#include "BrowserKindCatalog.h"
#include "../plugin_session/RailTieHeights.h"
#include "PluginCatalog.h"
#include "ThumbnailView.h"

namespace rs::app::plugin_browser {


// LayingSet: 敷設プラグインセットの合成プレビュー(複数フォルダを1枚に撮る)。
// TrainTemplate・RailTemplate: テンプレートのカードの画像(編成・敷設のテンプレートのファイル1本を1枚に撮る)。
enum class ImageKind { Icon, Thumbnail, LayingSet, TrainTemplate, RailTemplate };

struct PluginImageRequest {
    EntryId id = 0;
    std::uint64_t generation = 0;
    ImageKind kind = ImageKind::Icon;
    std::string folderUtf8;
    std::string iconPathUtf8;
    float iconRect[4] = {0.0f, 0.0f, 1.0f, 1.0f}; // アイコンに使う範囲(IconRect)
    BrowserKind browserKind = BrowserKind::Rail;
    ThumbnailView view = ThumbnailView::Standard; // Thumbnail のときの撮り方
    // LayingSet のときの材料。フォルダの並びと、Rail・Tie の最大高さ(沈め量・架線の持ち上げ量の元)。
    std::vector<std::string> layingFolders;
    rs::app::RailTieHeights layingHeights;
    std::uint64_t layingSerial = 0; // 要求の通し番号(結果にそのまま返す)
    // TrainTemplate・RailTemplate のときの材料。テンプレートのファイルとプラグインルート、結果に返す識別子。
    std::string templatePathUtf8;
    std::string templateRootUtf8;
    std::uint64_t templateKey = 0;
};

struct PluginImageResult {
    EntryId id = 0;
    std::uint64_t generation = 0;
    ImageKind kind = ImageKind::Icon;
    ThumbnailView view = ThumbnailView::Standard;
    std::uint64_t layingSerial = 0; // LayingSet のとき、要求の通し番号
    std::uint64_t templateKey = 0;  // TrainTemplate・RailTemplate のとき、要求の識別子
    bool ok = false;
    rs::render::ImageData image;
};

// 受付の振り分け。ClearPending はブラウザの画像(アイコン・サムネイル)だけ、ClearPendingLayingSet は合成だけを捨てる。
// テンプレートのカードの画像は、どちらでも捨てない(見せたいキーを置き場が毎フレーム出し直すので、捨てる側を持たない)。
inline bool IsLayingSetRequest(const PluginImageRequest& request) { return request.kind == ImageKind::LayingSet; }
inline bool IsBrowserImageRequest(const PluginImageRequest& request) {
    return request.kind == ImageKind::Icon || request.kind == ImageKind::Thumbnail;
}
inline bool IsTemplateImageKind(ImageKind kind) {
    return kind == ImageKind::TrainTemplate || kind == ImageKind::RailTemplate;
}

// 優先度(小さいほど先)。アイコンはサムネイルより先に出す。
inline constexpr int kIconPriority = 0;
inline constexpr int kThumbnailPriority = 1;
// 合成プレビューは画面の段にいま出すものなので、アイコンと同じくサムネイルより先に撮る。
inline constexpr int kLayingSetPreviewPriority = 0;
// テンプレートのカードは、ブラウザのカードのサムネイルと同じ優先度(同じ順に並んで撮る)。
inline constexpr int kTemplateImagePriority = kThumbnailPriority;

// 画像の受付の窓口。PluginImageStoreはこれだけを通して要求するので、テストでは要求を記録する
// 偽物に差し替えられる。
class IPluginImagePoster {
public:
    virtual ~IPluginImagePoster() = default;
    virtual void Post(PluginImageRequest request, int priority) = 0;
    // まだ処理を始めていない、アイコンとサムネイルの受付を捨てる(処理中の1件は止まらず、結果が遅れて届く)。
    // 合成プレビューの受付は残す。
    virtual void ClearPending() = 0;
    // まだ処理を始めていない、合成プレビューの受付だけを捨てる。
    virtual void ClearPendingLayingSet() = 0;
};

class PluginImageWorker final : public IPluginImagePoster {
public:
    // device・shared・tonemap はサムネイルの撮影に使う(shared=本編のFrameRendererが持つPSO・ルート署名、
    // tonemap=本編と同じトーンマップのPSO・設定。
    // どちらもこのワーカーより長く生きていること)。device が空か shared・tonemap が nullptr なら、サムネイルは
    // 常に失敗として返す(カードはアイコンかフォールバック画像のまま)。
    PluginImageWorker(rs::rhi::Device device, const rs::render::MeshPipelineSharedState* shared,
                      const rs::render::TonemapPass* tonemap);

    void Post(PluginImageRequest request, int priority) override;
    void ClearPending() override;
    void ClearPendingLayingSet() override;

    // 出来上がった結果を最大maxCount件、出来上がった順に取り出す。
    std::vector<PluginImageResult> Drain(std::size_t maxCount);

private:
    // 以下の5つはワーカースレッドの開始・終了・受付処理からだけ呼ぶ。
    PluginImageResult RenderThumbnail(const PluginImageRequest& request, std::stop_token stopToken);
    PluginImageResult RenderLayingSet(const PluginImageRequest& request, std::stop_token stopToken);
    PluginImageResult RenderTemplate(const PluginImageRequest& request, std::stop_token stopToken);
    void InitThumbnailRenderer();
    // 解像度ごとの撮影器。初めて使うときに作る(作れなかった解像度は以後 nullptr を返す)。
    rs::render::ThumbnailRenderer* RendererFor(std::uint32_t width, std::uint32_t height);

    struct SizedRenderer {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::unique_ptr<rs::render::ThumbnailRenderer> renderer; // 作れなかったら nullptr
    };

    // ワーカースレッドを起動するので、開始・終了フックが触るメンバより後ろに置く。
    rs::rhi::Device device_;
    const rs::render::MeshPipelineSharedState* shared_ = nullptr;
    const rs::render::TonemapPass* tonemap_ = nullptr;
    bool comInitialized_ = false;
    // ワーカースレッドだけが触る(解像度ごとに初回の撮影で作り、終了時に壊す)。
    // ThumbnailRenderer は解像度をInitで固定するため、変種の解像度の数だけ持つ。
    std::vector<SizedRenderer> thumbnailRenderers_;
    rs::platform_thread::WorkerThread<PluginImageRequest, PluginImageResult> worker_;
};

} // namespace rs::app::plugin_browser
