#include "Rs2LayoutDecode.h"

#include <filesystem>
#include <numbers>

#include <spdlog/spdlog.h>

#include "../../platform/text/Encoding.h"
#include "../../plugins/layout/Rs2Schema.h"
#include "../../plugins/layout/Rs2WarpImport.h"
#include "../../plugins/schema/parser/Parser.h"
#include "../scene/StructLinePoleTracker.h"
#include "InstallRoot.h"
#include "LayoutLoad.h"
#include "PluginSelector.h"
#include "Rs2TrainApplier.h"
#include "../../world/scene/TrainTagRegistrySerialize.h"

namespace rs::app {
namespace {

// 編成の取り込みは、車両の長さを知るために選択器へ編成を足して作る。デコードが終わったら(失敗や例外でも)
// 足した編成と選択位置を元に戻し、現在の編成には触れなかったことにする。取り込んだ編成は定義(JSON)として
// NormalizedLayoutへ渡し、ApplyNormalizedLayoutが確定後に作り直す。
class FormationStagingScope {
public:
    explicit FormationStagingScope(PluginSelector& selector)
        : selector_(selector),
          firstIndex_(selector.Formations().size()),
          selectedIndex_(selector.SelectedFormationIndex()) {}
    ~FormationStagingScope() {
        RemoveFormationsFrom(selector_, firstIndex_);
        selector_.SelectFormation(selectedIndex_);
    }
    FormationStagingScope(const FormationStagingScope&) = delete;
    FormationStagingScope& operator=(const FormationStagingScope&) = delete;

    nlohmann::json StagedFormationsJson() const { return FormationsToJson(selector_, firstIndex_); }

private:
    PluginSelector& selector_;
    size_t firstIndex_;
    size_t selectedIndex_;
};

}  // namespace

LayoutActionResult DecodeRs2(const std::string& rs2PathUtf8, PluginSelector& selector,
                         NormalizedLayout& normalized,
                         const std::vector<std::string>& pluginRootCandidatesUtf8) {
    // 受け取った中身は使わず、既定値から作り直す。
    normalized = NormalizedLayout{};
    const FormationStagingScope formationStaging(selector);
    LayoutActionResult result = LayoutActionResult::Failure(LayoutActionStatus::ImportFailed, rs2PathUtf8);
    const std::wstring path = rs::platform_text::Utf8ToWide(rs2PathUtf8);
    rs::legacy_pi::Diagnostics diag;
    rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(path, diag);
    if (diag.HasErrors()) {
        result.warnings.push_back("PIパース失敗: " + rs2PathUtf8);
        return result;
    }

    std::vector<std::string> schemaWarnings;
    rs::legacy_layout::Rs2Layout layout = rs::legacy_layout::ParseRs2Layout(doc, schemaWarnings);
    result.warnings.insert(result.warnings.end(), schemaWarnings.begin(), schemaWarnings.end());
    if (layout.scenes.empty()) {
        result.warnings.push_back("Sceneが見つかりません: " + rs2PathUtf8);
        return result;
    }

    std::vector<std::string> timeWarnings;
    rs::legacy_layout::ApplyRs2TimeToGameClock(layout.time, normalized.gameClock, normalized.solarOverrides,
                                                timeWarnings);
    result.warnings.insert(result.warnings.end(), timeWarnings.begin(), timeWarnings.end());

    // 下流は単一ルートしか受け取れないため、レール系フォルダの存在で候補を1つに絞る。
    std::string installRootUtf8;
    for (const std::string& candidateUtf8 : pluginRootCandidatesUtf8) {
        const std::filesystem::path candidate = rs::platform_text::Utf8ToWide(candidateUtf8);
        if (std::filesystem::exists(candidate / L"Rail")) {
            installRootUtf8 = candidateUtf8;
            break;
        }
    }
    if (installRootUtf8.empty()) {
        installRootUtf8 = pluginRootCandidatesUtf8.empty()
            ? rs::platform_text::WideToUtf8(ResolveInstallRootFromFilePath(path).wstring())
            : pluginRootCandidatesUtf8.front();
    }
    size_t activeIndex = 0;
    if (!layout.currentScene.empty()) {
        for (size_t i = 0; i < layout.scenes.size(); ++i) {
            if (layout.scenes[i].address == layout.currentScene) {
                activeIndex = i;
                break;
            }
        }
    }

    normalized.scenes.reserve(layout.scenes.size());
    std::vector<rs::legacy_layout::AddressMap> sceneAddresses;
    sceneAddresses.reserve(layout.scenes.size());
    std::vector<rs::legacy_layout::Rs2PendingTrainRoutines> scenePendingTrainRoutines;
    scenePendingTrainRoutines.reserve(layout.scenes.size());
    for (size_t i = 0; i < layout.scenes.size(); ++i) {
        const rs::legacy_layout::Rs2Scene& scene = layout.scenes[i];
        Rs2SceneImportResult imported = ImportRs2Scene(scene, installRootUtf8, selector);
        sceneAddresses.push_back(imported.addresses);
        scenePendingTrainRoutines.push_back(std::move(imported.pendingTrainRoutines));
        if (i == activeIndex) {
            result.warnings.insert(result.warnings.end(), imported.warnings.begin(), imported.warnings.end());
            result.nodeCount = imported.nodeCount;
            result.edgeCount = imported.edgeCount;
            result.placementCount = imported.placementCount;
            result.pluginFoldersLoaded = imported.pluginFoldersLoaded;
            result.pluginFoldersFailed = imported.pluginFoldersFailed;
            // 取り込み警告はアクティブSceneの分だけ出すので、警告済みのフォルダもその分だけ渡す。
            normalized.reportedTrackPluginFailures.insert(imported.failedPluginFolders.begin(),
                                                          imported.failedPluginFolders.end());
            result.placementsAdded = imported.placements.size();
            result.wireSpans = imported.wireSpans;
        }

        LayoutSceneCache cache;
        cache.name = scene.name;
        cache.envPlugin = scene.envPlugin;
        cache.graph = std::move(imported.graph);
        cache.overlay = std::move(imported.overlay);
        cache.manualScenery =
            SceneEntitiesFromImportedPlacements(imported.placements, scene.address);
        BindSceneLinePoles(cache.manualScenery, cache.graph, cache.overlay);
        cache.wireSpans = std::move(imported.wireSpans);
        cache.hasCamera = scene.camera.present;
        if (cache.hasCamera) {
            // RailSim II は視線の水平成分を(cos Head, sin Head)とする(Head=0で+X向き)。こちらの
            // yawは視線が-(sin yaw, cos yaw)なので、両者を一致させるyaw = -Head - π/2に直す。
            cache.camera.yaw = -scene.camera.head - std::numbers::pi_v<float> / 2.0f;
            cache.camera.pitch = scene.camera.pitch;
            cache.camera.distance = scene.camera.dist;
            cache.camera.target[0] = scene.camera.focus.x;
            cache.camera.target[1] = scene.camera.focus.y;
            cache.camera.target[2] = scene.camera.focus.z;
            if (scene.camera.fieldOfView > 0.0f) cache.camera.fieldOfView = scene.camera.fieldOfView;
        }
        normalized.scenes.push_back(std::move(cache));
    }

    std::vector<std::string> warpWarnings;
    normalized.warpLinks = rs::legacy_layout::ImportWarpLinks(layout.warps, sceneAddresses, warpWarnings);
    result.warnings.insert(result.warnings.end(), warpWarnings.begin(), warpWarnings.end());
    normalized.activeIndex = activeIndex;

    std::vector<rs::world::TrackGraph*> sceneGraphs;
    sceneGraphs.reserve(normalized.scenes.size());
    for (LayoutSceneCache& scene : normalized.scenes) sceneGraphs.push_back(&scene.graph);
    rs::world::TrainTagRegistry trainFormationLinks;
    // 停車中・発車直前だった編成の復元は、取り込んだ時計(停車時間の起点)を基準にする。
    Rs2TrainRestoreContext restoreContext;
    restoreContext.warpLinks = &normalized.warpLinks;
    restoreContext.nowAbsSeconds = normalized.gameClock.AbsoluteSeconds();
    restoreContext.timeScale = normalized.gameClock.TimeScale();
    restoreContext.runtime = &normalized.runtime;
    const Rs2TrainApplyResult trainResult = ApplyRs2TrainGroups(
        layout, installRootUtf8, selector, sceneGraphs, sceneAddresses, scenePendingTrainRoutines,
        normalized.cars,
        &normalized.placedTrainIds, &normalized.trainSpeedSeeds, &trainFormationLinks, restoreContext);
    normalized.trainTags = rs::world::TrainTagRegistrySnapshotOf(trainFormationLinks);
    normalized.formationDefsJson = formationStaging.StagedFormationsJson();
    result.warnings.insert(result.warnings.end(), trainResult.warnings.begin(), trainResult.warnings.end());
    // 件数の集計は異常ではないので、警告に混ぜるとユーザーへ毎回トーストが出てしまう。ログだけに出す。
    spdlog::info("[rs2] 編成 {} 件登録 / {} 編成 {} 両配置", trainResult.formationsAdded,
                 trainResult.trainsPlaced, trainResult.carsPlaced);
    // 取り込み警告はUIのログパネルにしか出ないため、ヘッドレス検証から読めるようログへも流す。
    for (const std::string& warning : result.warnings) spdlog::warn("[rs2] {}", warning);
    normalized.warnings = result.warnings;
    result.status = LayoutActionStatus::Ok;
    result.messageKey.clear();
    result.detail.clear();
    return result;
}

}  // namespace rs::app
