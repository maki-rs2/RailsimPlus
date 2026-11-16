//
// PluginBrowserService.h - ブラウザの裏側(カタログ・走査・画像・画面状態)を束ねて毎フレーム進める
//
// --rmlui-workspace のときだけ作る(ImGui版の画面では
// ブラウザを使わない)。画像のストア・ワーカーも持つ(走査・画像のワーカーは
// FrameRendererとRmlUiの終了より先に止める必要があるので、持ち主はフレームループの状態に置く)。
// メインスレッド専用。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "BrowserFrameBudget.h"
#include "BrowserProfileCatalog.h"
#include "LayingSetPreviewStore.h"
#include "TemplateImageStore.h"
#include "PluginBrowserState.h"
#include "PluginCatalog.h"
#include "PluginCatalogScanner.h"
#include "PluginImageStore.h"
#include "PluginImageWorker.h"
#include "../../plugins/layout/PluginTreeSchema.h"

namespace rs::app::plugin_browser {

class PluginBrowserService {
public:
    // device・shared・tonemap はサムネイルの撮影に使う(PluginImageWorker 参照)。shared・tonemap は本編の
    // FrameRenderer が持つもので、このサービスより長く生きていること。
    PluginBrowserService(rs::rhi::Device device, const rs::render::MeshPipelineSharedState* shared,
                         const rs::render::TonemapPass* tonemap)
        : imageWorker_(device, shared, tonemap) {}

    // 毎フレーム呼ぶ。
    //   - ブラウザが初めて見えたフレームで走査を始める(起動直後には走査しない)
    //   - 始めたあと、pluginRootsが前回の写しと変わった(または再走査の要求があった)ら、
    //     カタログを空にして最初から走査する
    //   - プロファイルが変わったら、そのプロファイルの種別から先に走査する
    //   - 走査の結果を受け取ってカタログへ取り込む
    void Tick(const std::vector<std::string>& pluginRoots, BrowserProfile activeProfile, bool browserVisible);

    // 次のTickでカタログを空にして走査し直す(ルート管理ウィンドウの再走査ボタン用)。
    void Rescan() { rescanRequested_ = true; }

    bool Scanning() const { return started_ && scanner_.Scanning(); }
    // 走査を始めたか(ブラウザが一度でも見えた)。
    bool Started() const { return started_; }
    // 走査に使っているルートの写し(今のTickの時点)。
    const std::vector<std::string>& Roots() const { return roots_; }

    PluginCatalog& Catalog() { return catalog_; }
    const PluginCatalog& Catalog() const { return catalog_; }
    // ディレクトリ分けの木(Config2k.txt / Config.txt の *PluginTree)。走査を始めるたびに読み直し、
    // 無い・読めないときはnullptr。版は読み直すたびに増える。
    const rs::legacy_layout::PluginTreeSet* Trees() const { return trees_.has_value() ? &*trees_ : nullptr; }
    std::uint64_t TreesRevision() const { return treesRevision_; }

    PluginBrowserState& State() { return state_; }
    const PluginBrowserState& State() const { return state_; }

    // 画像(アイコン・サムネイル)。ストアはメインスレッド専用、ワーカーへの要求はストアが出す。
    PluginImageStore& Images() { return images_; }
    const PluginImageStore& Images() const { return images_; }
    PluginImageWorker& ImageWorker() { return imageWorker_; }

    // 敷設プラグインセットの合成プレビュー。ブラウザを開いていなくても結果を受け取る(PumpImagesが毎フレーム回る)。
    LayingSetPreviewStore& LayingPreview() { return layingPreview_; }
    const LayingSetPreviewStore& LayingPreview() const { return layingPreview_; }

    // テンプレートのカード(編成・敷設)の画像。結果の受け取りは敷設セットの合成と同じく毎フレーム回る。
    TemplateImageStore& TemplateImages() { return templateImages_; }
    const TemplateImageStore& TemplateImages() const { return templateImages_; }

    // 差し替わった画像のEntryIdを取り出して空にする(ビューがRmlUiのテクスチャを解放する)。
    // ブラウザが見えないフレームのぶんも溜めておく。
    std::vector<EntryId> TakeReplacedImages();

    // RmlUiのテクスチャの引き先。"pbimg:<世代>/<EntryId>[/<変種>]"(PluginImageSource.h)で、今の世代のRGBAを持っているときだけ
    // outへ写してtrueを返す(それ以外はfalse)。合成プレビューの名前("lsimg:<番号>")・テンプレートのカードの名前("tmimg:<キー>")もここで引く。RmlUiのレンダラから呼ばれる。
    bool ResolveImageSource(const std::string& source, rs::render::ImageData& out) const;

private:
    void StartScan(bool resetCatalog);
    void PumpImages(const FrameBudget& budget);

    PluginCatalog catalog_;
    PluginBrowserState state_;
    std::optional<rs::legacy_layout::PluginTreeSet> trees_;
    std::uint64_t treesRevision_ = 0;
    std::vector<std::string> roots_;      // 走査に使っているルートの写し
    BrowserProfile priorityProfile_ = BrowserProfile::None; // 走査の優先に使っているプロファイル
    bool started_ = false;
    bool rescanRequested_ = false;
    std::uint64_t imageGeneration_ = 0;      // 画像のストアが対応している世代
    std::vector<EntryId> replacedImages_;   // 差し替わって、まだビューが解放していない画像

    PluginImageStore images_;
    LayingSetPreviewStore layingPreview_;
    TemplateImageStore templateImages_;

    // ワーカーを持つので最後に置いて、最初に止める。
    PluginCatalogScanner scanner_;
    PluginImageWorker imageWorker_;
};

} // namespace rs::app::plugin_browser
