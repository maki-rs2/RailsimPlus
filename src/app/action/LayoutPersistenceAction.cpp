//
// LayoutPersistenceAction.cpp - レイアウト読込・保存とScene切替のアプリサービス
//
#include "LayoutPersistenceAction.h"
#include "LayoutSavePath.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <utility>

#include "../../editor/tool/Editor.h"
#include "../../platform/text/AsciiCase.h"
#include "../../platform/text/Encoding.h"
#include "../../world/alignment/GuidJson.h"
#include "../../world/scene/SceneRegistry.h"
#include "../../world/time/GameClock.h"
#include "../camera/CameraControl.h"
#include "../plugin_session/ActiveEnv.h"
#include "../plugin_session/DefaultPluginCatalog.h"
#include "../plugin_session/InstallRoot.h"
#include "../plugin_session/PluginSelector.h"
#include "../plugin_session/PluginRootResolver.h"
#include "../plugin_session/RslLayoutSerialize.h"
#include "../plugin_session/AutoRecovery.h"
#include "../plugin_session/RslLayoutLoad.h"
#include "../plugin_session/LayoutFileCatalog.h"
#include "../plugin_session/LayoutLoad.h"
#include "../plugin_session/Rs2LayoutDecode.h"
#include "../settings/UserSettings.h"
#include "../ui/FormationPanel.h"
#include "../scene/SceneRenderer.h"

namespace rs::app {
namespace {

std::filesystem::path ToFilesystemPath(const std::string& pathUtf8) {
    return std::filesystem::path(rs::platform_text::Utf8ToWide(pathUtf8));
}

bool IsReadableFile(const std::string& pathUtf8) {
    return std::filesystem::is_regular_file(ToFilesystemPath(pathUtf8));
}

}  // namespace

LayoutPersistenceAction::LayoutPersistenceAction(LayoutPersistenceRefs refs) : refs_(refs) {}

LayoutActionResult LayoutPersistenceAction::ApplyLayout(NormalizedLayout&& layout, const rs::world::Guid& documentGuid,
                                                        const std::string& savedPathUtf8,
                                                        const std::string& displayNameUtf8) {
    // 読込・新規・復元は別のレイアウトへ入れ替えるので、削除の控えは持ち越さない。
    deletedScene_.reset();
    const LayoutApplyRefs applyRefs{refs_.layoutSession, refs_.editor, refs_.sceneRegistry,
                                    refs_.gameClock, refs_.solarOverrides,
                                    refs_.sceneRenderer, refs_.camera, refs_.selector,
                                    &refs_.trainPlacementUi, &refs_.importedWireSpans, {}};
    try {
        LayoutActionResult result = ApplyNormalizedLayout(std::move(layout), applyRefs);
        refs_.document.guid = documentGuid;
        refs_.document.savedPathUtf8 = savedPathUtf8;
        refs_.document.displayNameUtf8 = displayNameUtf8;
        refs_.document.Rebase(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision());
        // 読込ではViewModeが変わらずEditorModeSignatureの比較で解除されないため、ここで解除する。
        refs_.editor.ClearSelectionAndHover();
        return result;
    } catch (...) {
        // 反映の途中で失敗すると、ライブ状態は前のレイアウトとも読込先とも言えない半端な状態になる。
        // 前のファイルへ上書き保存されないよう、保存先なしの未保存の新しい文書として扱う。
        refs_.document.guid = rs::world::GenerateGuid();
        refs_.document.savedPathUtf8.clear();
        refs_.document.displayNameUtf8.clear();
        refs_.document.Rebase(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision());
        refs_.document.MarkEdited();
        refs_.editor.ClearSelectionAndHover();
        throw;
    }
}

std::vector<FinishedLayoutResult> LayoutPersistenceAction::TakeFinishedResults() {
    return std::exchange(finishedResults_, {});
}

RslPersistenceRefs LayoutPersistenceAction::MakePersistenceRefs(
    const std::vector<std::string>& pluginRootCandidates) const {
    return RslPersistenceRefs{refs_.layoutSession, refs_.editor,         refs_.sceneRegistry,
                              refs_.gameClock,    refs_.solarOverrides, refs_.sceneRenderer,
                              refs_.camera,       refs_.selector,       &refs_.importedWireSpans,
                              pluginRootCandidates, &refs_.document.guid};
}

LayoutActionResult LayoutPersistenceAction::LoadLayout(const std::string& pathUtf8) {
    LayoutActionResult result = RunLoadLayout(pathUtf8);
    finishedResults_.push_back({FinishedLayoutKind::Load, result});
    return result;
}

LayoutActionResult LayoutPersistenceAction::RunLoadLayout(const std::string& pathUtf8) {
    // 読込ジョブ進行中の多重読込要求は拒否する。ロールバックは実装しない。
    if (refs_.sceneRenderer.SyncJobActive()) {
        return LayoutActionResult::Failure(LayoutActionStatus::Busy, pathUtf8);
    }
    try {
        if (!IsReadableFile(pathUtf8)) return LayoutActionResult::Failure(LayoutActionStatus::FileNotFound, pathUtf8);
        const std::filesystem::path layoutPath = ToFilesystemPath(pathUtf8);
        std::string extension = layoutPath.extension().string();
        extension = rs::platform_text::ToAsciiLower(extension);
        const std::string derivedRootUtf8 = extension == ".rs2"
            ? rs::platform_text::WideToUtf8(ResolveInstallRootFromFilePath(layoutPath).wstring())
            : std::string{};
        const std::string executableDirectoryUtf8 =
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
        const std::vector<std::string> candidates = BuildPluginRootCandidates(
            derivedRootUtf8, refs_.userSettings.pluginRoots, executableDirectoryUtf8);
        NormalizedLayout normalized;
        rs::world::Guid loadedGuid = rs::world::NilGuid();
        size_t placementCount = 0;
        if (extension == ".rsl") {
            const RslPersistenceRefs persistenceRefs = MakePersistenceRefs(candidates);
            const LayoutActionResult decodeResult = DecodeRsl(pathUtf8, persistenceRefs, normalized);
            if (!decodeResult.Ok()) {
                return decodeResult;
            }
            placementCount = decodeResult.placementCount;
            loadedGuid = decodeResult.guid;
        } else if (extension == ".rs2") {
            const LayoutActionResult decodeResult = DecodeRs2(pathUtf8, refs_.selector, normalized, candidates);
            if (!decodeResult.Ok()) return decodeResult;
            placementCount = decodeResult.placementCount;
        } else {
            return LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8);
        }
        // 各SceneのEnvPluginをEnv/<名前>/のフォルダへ解決する(.rs2・.rslで共通。実際の読込は
        // ActiveEnv::Updateがアクティブなときに行う)。
        ResolveSceneEnvFolders(normalized.scenes, candidates);
        const bool isRsl = extension == ".rsl";
        LayoutActionResult result = ApplyLayout(std::move(normalized),
                                                isRsl ? loadedGuid : rs::world::GenerateGuid(),
                                                isRsl ? pathUtf8 : std::string{},
                                                isRsl ? std::string{} : LayoutNameFromPath(pathUtf8));
        result.placementCount = placementCount;
        return result;
    } catch (const std::exception& exception) {
        return LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what());
    }
}

LayoutActionResult LayoutPersistenceAction::NewLayout() {
    LayoutActionResult result = RunNewLayout();
    finishedResults_.push_back({FinishedLayoutKind::New, result});
    return result;
}

bool LayoutPersistenceAction::NewLayoutIfPristine() {
    if (HasPendingLoad() || refs_.sceneRenderer.SyncJobActive()) return false;
    const LayoutDocumentState& document = refs_.document;
    if (!document.savedPathUtf8.empty() || !document.displayNameUtf8.empty()) return false;
    if (document.IsDirty(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision())) return false;
    NewLayout();
    return true;
}

LayoutActionResult LayoutPersistenceAction::RunNewLayout() {
    if (refs_.sceneRenderer.SyncJobActive()) return LayoutActionResult::Failure(LayoutActionStatus::Busy, {});
    try {
        // 空のSceneを1つ持つレイアウトを、読込と同じ適用経路へ渡す。
        NormalizedLayout normalized;
        // 適用は時計と太陽設定を丸ごと差し替えるため、既定値(1970年0時)で上書きされないよう現在の値を引き継ぐ。
        normalized.gameClock = refs_.gameClock;
        normalized.solarOverrides = refs_.solarOverrides;
        LayoutSceneCache emptyScene;
        emptyScene.guid = rs::world::GenerateGuid();
        normalized.scenes.push_back(std::move(emptyScene));
        LayoutActionResult result = ApplyLayout(std::move(normalized), rs::world::GenerateGuid(), {}, {});

        const std::string executableDirectoryUtf8 =
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
        const std::vector<std::string> candidates = BuildPluginRootCandidates(
            {}, refs_.userSettings.pluginRoots, executableDirectoryUtf8);
        EnsureDefaultPlacements(refs_.sceneRegistry, refs_.userSettings, candidates);
        // 敷設プラグインセットが空のときだけ、RailSim2で選択中だったプラグインを初期値にする。
        if (refs_.editor.layingSettings.defaultTrackPlugins.empty()) {
            std::string loadError;
            for (const std::string& folder : ResolveConfigTrackPluginFolders(candidates)) {
                const std::optional<std::string> registered =
                    refs_.selector.EnsureTrackPluginLoaded(folder, loadError);
                if (!registered) continue;
                refs_.editor.layingSettings.defaultTrackPlugins.push_back(*registered);
            }
        }

        // 既定の配置と敷設プラグインの追加は編集ではないので、基準へ取り込む。
        refs_.document.Rebase(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision());
        return result;
    } catch (const std::exception& exception) {
        return LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what());
    }
}

std::vector<std::string> LayoutPersistenceAction::PluginRootCandidates() const {
    const std::string executableDirectoryUtf8 =
        rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
    return BuildPluginRootCandidates({}, refs_.userSettings.pluginRoots, executableDirectoryUtf8);
}

LayoutActionResult LayoutPersistenceAction::AddScene(const std::string& surfaceFolderUtf8) {
    if (!PrepareSceneOperation() || refs_.sceneRenderer.SyncJobActive()) {
        return LayoutActionResult::Failure(LayoutActionStatus::Busy, {});
    }
    if (surfaceFolderUtf8.empty()) return LayoutActionResult::Failure(LayoutActionStatus::FileNotFound, {});
    try {
        AddLayoutScene(surfaceFolderUtf8, refs_.editor, refs_.sceneRegistry, refs_.camera, refs_.importedWireSpans,
                       refs_.layoutSession);
        deletedScene_.reset();
        // 切替で動いたrevisionは基準に取り込むが、Sceneを足したこと自体は未保存の編集として残す
        // (追加前から未保存だった場合も、そのまま未保存)。
        FinishSceneSwitch(true);
        LayoutActionResult result;
        result.sceneCount = refs_.layoutSession.scenes.size();
        return result;
    } catch (const std::exception& exception) {
        return LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what());
    }
}

LayoutActionResult LayoutPersistenceAction::SaveRsl(const std::string& pathUtf8, const std::string* thumbnailBase64) {
    // 読込ジョブの途中は配置物が半端なので、保存しない(自動復元も同じ理由で止めている)。
    if (refs_.sceneRenderer.SyncJobActive()) return LayoutActionResult::Failure(LayoutActionStatus::Busy, pathUtf8);
    try {
        const std::filesystem::path resolvedPath = ResolveLayoutSavePath(pathUtf8, SaveDirectory());
        if (resolvedPath.empty()) return LayoutActionResult::Failure(LayoutActionStatus::InvalidFormat, pathUtf8);

        std::error_code error;
        std::filesystem::create_directories(resolvedPath.parent_path(), error);
        if (error) return LayoutActionResult::Failure(LayoutActionStatus::FileWriteFailed, error.message());

        const std::string resolvedPathUtf8 = rs::platform_text::WideToUtf8(resolvedPath.wstring());
        const std::string executableDirectoryUtf8 =
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
        const std::vector<std::string> candidates = BuildPluginRootCandidates(
            {}, refs_.userSettings.pluginRoots, executableDirectoryUtf8);
        RslPersistenceRefs persistenceRefs = MakePersistenceRefs(candidates);
        persistenceRefs.thumbnailBase64 = thumbnailBase64;
        LayoutActionResult saveResult = SaveRslLayout(resolvedPathUtf8, persistenceRefs);
        if (saveResult.Ok()) {
            saveResult.savedPathUtf8 = resolvedPathUtf8;
            refs_.document.savedPathUtf8 = resolvedPathUtf8;
            refs_.document.displayNameUtf8.clear();
            refs_.document.Rebase(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision());
            ClearAutoRecovery(refs_.autoRecoveryRoot, refs_.document.guid);
        }
        return saveResult;
    } catch (const std::exception& exception) {
        return LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what());
    }
}

std::filesystem::path LayoutPersistenceAction::SaveDirectory() const {
    return refs_.userSettings.layoutSaveDirectory.empty() ? UserSettings::DefaultLayoutSaveDirectory()
                                                          : ToFilesystemPath(refs_.userSettings.layoutSaveDirectory);
}

std::string LayoutPersistenceAction::ResolveSavePath(const std::string& inputUtf8) const {
    return rs::platform_text::WideToUtf8(ResolveLayoutSavePath(inputUtf8, SaveDirectory()).wstring());
}

const std::string& LayoutPersistenceAction::DocumentSavedPath() const { return refs_.document.savedPathUtf8; }

std::string LayoutPersistenceAction::DocumentDisplayName() const {
    return refs_.document.savedPathUtf8.empty() ? refs_.document.displayNameUtf8
                                                : LayoutNameFromPath(refs_.document.savedPathUtf8);
}

void LayoutPersistenceAction::DiscardCurrentRecovery() {
    ClearAutoRecovery(refs_.autoRecoveryRoot, refs_.document.guid);
}

void LayoutPersistenceAction::DiscardRecovery(const rs::world::Guid& guid) {
    ClearAutoRecovery(refs_.autoRecoveryRoot, guid);
}

LayoutActionResult LayoutPersistenceAction::WriteAutoRecoverySnapshot() {
    try {
        const std::filesystem::path snapshotPath =
            AutoRecoverySnapshotPath(refs_.autoRecoveryRoot, refs_.document.guid);
        std::error_code error;
        std::filesystem::create_directories(snapshotPath.parent_path(), error);
        if (error) return LayoutActionResult::Failure(LayoutActionStatus::FileWriteFailed, error.message());
        const std::string snapshotPathUtf8 = rs::platform_text::WideToUtf8(snapshotPath.wstring());
        const std::string executableDirectoryUtf8 =
            rs::platform_text::WideToUtf8(UserSettings::ExecutableDirectory().wstring());
        const std::vector<std::string> candidates = BuildPluginRootCandidates(
            {}, refs_.userSettings.pluginRoots, executableDirectoryUtf8);
        const RslPersistenceRefs persistenceRefs = MakePersistenceRefs(candidates);
        LayoutActionResult result = SaveRslLayout(snapshotPathUtf8, persistenceRefs);
        if (!result.Ok()) return result;
        result = WriteAutoRecoverySource(refs_.autoRecoveryRoot, refs_.document.guid, refs_.document.savedPathUtf8,
                                         refs_.document.displayNameUtf8);
        if (result.Ok()) result.savedPathUtf8 = snapshotPathUtf8;
        return result;
    } catch (const std::exception& exception) {
        return LayoutActionResult::Failure(LayoutActionStatus::Unexpected, exception.what());
    }
}

LayoutDocumentViewModel LayoutPersistenceAction::DocumentViewModel() const {
    return {refs_.document.IsDirty(refs_.editor.DocumentRevision(), refs_.sceneRegistry.PersistentRevision())};
}

void LayoutPersistenceAction::RequestLoadLayout(const std::string& pathUtf8, bool discardCurrentRecovery) {
    if (pendingLoad_) return;
    pendingLoad_ = PendingLayoutLoad{};
    pendingLoad_->pathUtf8 = pathUtf8;
    pendingLoad_->discardCurrentRecovery = discardCurrentRecovery;
}

void LayoutPersistenceAction::RequestRestoreRecovery(const AutoRecoveryCandidate& candidate) {
    if (pendingLoad_) return;
    pendingLoad_ = PendingLayoutLoad{};
    pendingLoad_->pathUtf8 = rs::platform_text::WideToUtf8(candidate.snapshotPath.wstring());
    pendingLoad_->restoring = true;
    pendingLoad_->restoredSavedPathUtf8 = candidate.sourcePathUtf8;
    pendingLoad_->restoredDisplayNameUtf8 = candidate.displayNameUtf8;
}

bool LayoutPersistenceAction::HasPendingLoad() const { return pendingLoad_.has_value(); }

bool LayoutPersistenceAction::IsLoading() const {
    return pendingLoad_.has_value() || loadAppliedThisFrame_ || refs_.sceneRenderer.SyncJobActive();
}

void LayoutPersistenceAction::MarkPendingLoadIndicatorShown() {
    if (pendingLoad_) pendingLoad_->indicatorShown = true;
}

void LayoutPersistenceAction::TickPendingLoad() {
    loadAppliedThisFrame_ = false;
    if (!pendingLoad_ || !pendingLoad_->indicatorShown) return;
    const PendingLayoutLoad pending = *pendingLoad_;
    pendingLoad_.reset();
    const rs::world::Guid previousGuid = refs_.document.guid;
    LayoutActionResult result = LoadLayout(pending.pathUtf8);
    loadAppliedThisFrame_ = result.Ok();
    if (result.Ok() && pending.discardCurrentRecovery) ClearAutoRecovery(refs_.autoRecoveryRoot, previousGuid);
    if (result.Ok() && pending.restoring) {
        refs_.document.savedPathUtf8 = pending.restoredSavedPathUtf8;
        refs_.document.displayNameUtf8 = pending.restoredDisplayNameUtf8;
        refs_.document.MarkEdited();
    }
}

LayoutSceneViewModel LayoutPersistenceAction::SceneViewModel() const {
    LayoutSceneViewModel viewModel;
    viewModel.activeSceneIndex = refs_.layoutSession.activeIndex;
    viewModel.sceneCount = refs_.layoutSession.scenes.size();
    viewModel.hasMultipleScenes = refs_.layoutSession.HasMultipleScenes();
    if (!refs_.layoutSession.scenes.empty()) {
        viewModel.activeSceneName = refs_.layoutSession.scenes[refs_.layoutSession.activeIndex].name;
    }
    return viewModel;
}

}  // namespace rs::app
