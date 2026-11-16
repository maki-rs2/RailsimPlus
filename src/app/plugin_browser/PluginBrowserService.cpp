#include "PluginBrowserService.h"

#include <optional>
#include <utility>

#include "PluginImageSource.h"
#include "PluginTreeConfigSource.h"
#include "../scene/ParsedPluginCache.h"

namespace rs::app::plugin_browser {
namespace {

// プロファイルの種別タブを走査の種別へ直したもの(信号用の種別はStationの走査で賄う)。
std::vector<BrowserKind> PriorityKindsOf(BrowserProfile profile) {
    std::vector<BrowserKind> kinds;
    const BrowserProfileTraits& traits = GetBrowserProfile(profile);
    for (std::size_t i = 0; i < traits.tabCount; ++i) {
        const std::optional<BrowserKind> scanKind = BrowserKindFromPluginType(GetBrowserKind(traits.tabs[i]).pluginType);
        if (scanKind.has_value()) kinds.push_back(*scanKind);
    }
    return kinds;
}

} // namespace

void PluginBrowserService::StartScan(bool resetCatalog) {
    if (resetCatalog) {
        GetParsedPluginCache().Invalidate();
        catalog_.Reset();
    }
    trees_ = LoadPluginTreeSet(roots_);
    ++treesRevision_;
    priorityProfile_ = state_.Profile();
    scanner_.Start(roots_, catalog_.Generation(), PriorityKindsOf(priorityProfile_));
}

void PluginBrowserService::Tick(const std::vector<std::string>& pluginRoots, BrowserProfile activeProfile,
                                bool browserVisible) {
    state_.SetProfile(activeProfile);

    if (!started_) {
        if (!browserVisible) {
            // ブラウザを開く前でも、合成プレビュー・テンプレートの画像の結果は受け取る(走査は始めないので画像の要求はブラウザ側に無い)。
            PumpImages(FrameBudget());
            return;
        }
        started_ = true;
        roots_ = pluginRoots;
        StartScan(false);
    } else if (rescanRequested_ || pluginRoots != roots_) {
        roots_ = pluginRoots;
        StartScan(true);
    } else if (priorityProfile_ != activeProfile) {
        priorityProfile_ = activeProfile;
        scanner_.SetPriorityKinds(PriorityKindsOf(activeProfile));
    }
    rescanRequested_ = false;

    // 走査と画像で1つの予算を分け合う。走査が使い切っても、画像は最低1件は受け取る。
    const FrameBudget budget;
    scanner_.Tick(catalog_, budget);
    PumpImages(budget);
}

void PluginBrowserService::PumpImages(const FrameBudget& budget) {
    // 世代が変わったら(ルートが変わって走査をやり直したら)、前の世代の画像と未処理の受付を捨てる。
    if (catalog_.Generation() != imageGeneration_) {
        imageGeneration_ = catalog_.Generation();
        images_.Reset();
        imageWorker_.ClearPending();
        replacedImages_.clear();
    }
    // 1件ずつ受け取って取り込み、予算を超えたら止める(最低1件は受け取る)。合成プレビューは置き場へ、
    // それ以外(アイコン・サムネイル)は画像のストアへ振り分ける。
    do {
        std::vector<PluginImageResult> one = imageWorker_.Drain(1);
        if (one.empty()) break;
        if (one.front().kind == ImageKind::LayingSet) {
            layingPreview_.Accept(one.front());
            continue;
        }
        if (IsTemplateImageKind(one.front().kind)) {
            templateImages_.Accept(one.front());
            continue;
        }
        const std::vector<EntryId> replaced = images_.Accept(std::move(one), imageGeneration_);
        replacedImages_.insert(replacedImages_.end(), replaced.begin(), replaced.end());
    } while (!budget.Exceeded());
}

std::vector<EntryId> PluginBrowserService::TakeReplacedImages() {
    std::vector<EntryId> taken;
    taken.swap(replacedImages_);
    return taken;
}

bool PluginBrowserService::ResolveImageSource(const std::string& source, rs::render::ImageData& out) const {
    const std::optional<PluginImageSource> parsed = ParseImageSourceName(source);
    if (!parsed.has_value()) {
        const rs::render::ImageData* layingImage = layingPreview_.FindByName(source);
        if (layingImage == nullptr) layingImage = templateImages_.FindByName(source);
        if (layingImage == nullptr) return false;
        out = *layingImage;
        return true;
    }
    if (parsed->generation != imageGeneration_) return false;
    const rs::render::ImageData* image = images_.Find(parsed->key);
    if (image == nullptr) return false;
    out = *image;
    return true;
}

} // namespace rs::app::plugin_browser
