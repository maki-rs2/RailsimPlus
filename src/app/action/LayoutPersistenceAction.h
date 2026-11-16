//
// LayoutPersistenceAction.h - レイアウト読込・保存とScene切替のアプリサービス
//
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "LayoutActionResult.h"
#include "../plugin_session/AutoRecovery.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/SceneDeletionBackup.h"
#include "../scene/SceneSlotSynchronizer.h"
#include "../plugin_session/LayoutDocumentState.h"
#include "../plugin_session/RslLayoutSerialize.h"
#include "ScenePanelViewModel.h"

namespace rs::editor { class Editor; class WarpLinkAuthoring; }
namespace rs::app::workspace { struct ScenePanelPresentation; }
namespace rs::world { class GameClock; class SceneRegistry; struct EnvironmentPreset; struct SolarOverrides; }

namespace rs::app {

class CabinViewCamera;
class CameraController;
struct TrainPlacementUiState;
struct NormalizedLayout;
class PluginSelector;
class SceneRenderer;
struct UserSettingsData;

struct LayoutSceneViewModel {
    size_t activeSceneIndex = 0;
    size_t sceneCount = 0;
    std::string activeSceneName;
    bool hasMultipleScenes = false;
};

// 読込ジョブ(フレーム分割)の進み具合。jobActiveが偽のときは、ほかの値に意味はない。
struct LayoutLoadProgress {
    bool jobActive = false;
    LoadJobPhase phase = LoadJobPhase::Prefetch;
    size_t done = 0;
    size_t total = 0;
};

struct LayoutDocumentViewModel {
    bool dirty = false;
};

// 読込・新規の結果の出どころ。結果の通知は出どころによらず同じ経路で行い、出どころは
// 「最近使ったファイルへ入れる」「復元の後始末」のような後処理の振り分けにだけ使う。
enum class FinishedLayoutKind { Load, New };

struct FinishedLayoutResult {
    FinishedLayoutKind kind = FinishedLayoutKind::Load;
    LayoutActionResult result;
};

struct LayoutPersistenceRefs {
    rs::editor::Editor& editor;
    PluginSelector& selector;
    rs::world::SceneRegistry& sceneRegistry;
    std::vector<ImportedWireSpan>& importedWireSpans;
    CameraController& camera;
    LayoutSession& layoutSession;
    TrainPlacementUiState& trainPlacementUi;
    SceneRenderer& sceneRenderer;
    rs::world::GameClock& gameClock;
    rs::world::SolarOverrides& solarOverrides;
    UserSettingsData& userSettings;
    LayoutDocumentState& document;
    const std::filesystem::path& autoRecoveryRoot;
    // Sceneの並べ替えで、保留中のワープ端点のScene番号を付け替える。
    rs::editor::WarpLinkAuthoring& warpAuthoring;
    // Sceneの削除で編成を消すとき、運転台視点の追従を外す(編成パネルの撤去と同じ経路を通す)。
    CabinViewCamera& cabinCamera;
    // 新規レイアウトで時計を戻す先の初期日時。
    const rs::world::EnvironmentPreset& environmentPreset;
};

class LayoutPersistenceAction {
public:
    explicit LayoutPersistenceAction(LayoutPersistenceRefs refs);
    LayoutActionResult LoadLayout(const std::string& pathUtf8);
    // 現在のレイアウトを空にして、既定プラグイン(DefaultPluginCatalog)を原点へ置く。
    // 起動時(何も開かないとき)と「新規レイアウト」操作で共用する。
    LayoutActionResult NewLayout();
    // 保存先も表示名も無く、編集もしていない新規のレイアウトのときだけ、NewLayout()をやり直す。
    // ルートが空のまま起動して既定プラグインを置けなかったあと、ルートを足したときに使う。
    // 読込予約・読込中・編集済み・ファイルから開いたレイアウトでは何もしない。やり直したときだけ真。
    bool NewLayoutIfPristine();
    // Surfaceを原点に置いた新しいSceneを末尾へ追加し、そのSceneへ切り替える。
    LayoutActionResult AddScene(const std::string& surfaceFolderUtf8);
    // thumbnailBase64はinfo.thumbnailへ入れる文字列(nullptrなら入れない)。
    LayoutActionResult SaveRsl(const std::string& pathUtf8, const std::string* thumbnailBase64 = nullptr);
    LayoutActionResult WriteAutoRecoverySnapshot();
    // 今のレイアウトの復元データを消す。
    void DiscardCurrentRecovery();
    void DiscardRecovery(const rs::world::Guid& guid);
    // 名前を付けて保存の保存先のフォルダ。
    std::filesystem::path SaveDirectory() const;
    // 入力された名前を、SaveRslが書き込むパスへ解決する(上書きの確認に使う)。解決できなければ空。
    std::string ResolveSavePath(const std::string& inputUtf8) const;
    // 開いているファイルのパス(未保存の新規や.rs2なら空)と、画面に出す名前(無ければ空)。
    const std::string& DocumentSavedPath() const;
    std::string DocumentDisplayName() const;
    void SwitchScene(int delta);
    // 行指定の切り替え。SwitchSceneと同じ本体を通る。
    void SwitchSceneTo(size_t index);
    // シーン名の変更(空の名前は受け付けない)。保存の対象になる編集で、Undoの対象にはしない。
    void RenameScene(size_t index, const std::string& name);
    // sourceのシーンを、一覧の見た目でdestinationの行の位置へ移す。今のシーンは変わらない。
    void ReorderScene(size_t source, size_t destination);
    // シーンを削除する(そのシーンを端に持つワープと、そこにいる列車も消える)。今のシーンなら隣へ切り替える。
    // シーンが1つしか無い・範囲外ならfalseで何も変えない。削除の直前の状態は、直近1回分だけ控える(.rslには保存しない)。
    // 保存の対象になる編集で、Undoの対象にはしない(元に戻すのは RestoreDeletedScene)。
    bool DeleteScene(size_t index);
    // 控えたシーンを削除前の位置へ戻す。控えが無い・古い(削除のあとに編集やシーン操作があった)ときはfalse。
    bool RestoreDeletedScene();
    // 戻せる控えがあるか(古い控えは、ここで捨てる)。
    bool HasRestorableScene();
    // 控えているシーンの名前。控えが無ければ空。
    std::string DeletedSceneName() const;
    // シーンの環境を変える(空=デフォルトの空)。保存の対象になる編集で、Undoの対象にはしない。
    // 今のシーンなら、描画へはActiveEnvが毎フレーム追従する。変わったときだけtrue。
    bool SetSceneEnvironment(size_t index, const std::string& envPlugin);
    // 選べる地形・環境をプラグインルートから走査し直す(シーン画面・追加のポップアップを開いたときに呼ぶ)。
    void RescanSceneChoices();
    // シーン画面の一覧をセッションから作り直す。変わったときだけtrue。
    bool FillScenePanel(workspace::ScenePanelPresentation& panel);
    LayoutSceneViewModel SceneViewModel() const;
    LayoutDocumentViewModel DocumentViewModel() const;

    // 読込を予約する(実行は次フレーム以降。起動時CLI(--openlayout)・「セーブデータ」
    // パネルのボタン、どちらもこれ経由にする。読込の経路を分けない)。
    // 既に予約が入っている間の再呼び出しは無視する(連打対策、ユーザー向けフィードバックは省略)。
    // discardCurrentRecoveryなら、読込に成功したあとで読込前のレイアウトの復元データを消す
    // (失敗したら読込前のレイアウトが未保存のまま残るので、消さない)。
    void RequestLoadLayout(const std::string& pathUtf8, bool discardCurrentRecovery = false);
    // 復元データを読み込む予約。読み込んだレイアウトは未保存の状態で、保存先は元の文書のものになる
    // (スナップショットの場所を保存先にすると、保存の整理で書いたファイルごと消えてしまう)。
    void RequestRestoreRecovery(const AutoRecoveryCandidate& candidate);

    // 読込ジョブの進み具合(進捗表示用)。
    LayoutLoadProgress LoadProgress() const;
    // 読込の予約が入っている間か、読込ジョブが進んでいる間(新しい読込・保存の要求を受けられない)。
    bool IsLoading() const;

    // 読込予約が入っているか(UIが「読込中」表示を出すかの判定に使う)。
    bool HasPendingLoad() const;

    // 「読込中」インジケータが実際にこのフレームで描画されたことを記録する
    // (DrawPendingLayoutLoadIndicatorから呼ぶ。フレーム末尾のPresentで画面に
    // 出た後のフレームでなければTickPendingLoad()は実処理へ進めない)。
    void MarkPendingLoadIndicatorShown();

    // 毎フレーム先頭(NewFrame群より前)で呼ぶ。インジケータが1回でも描画済みの
    // 予約があれば、そこでLoadLayout()を実行する。
    void TickPendingLoad();

    // LoadLayout()・NewLayout()が終えた結果を、終えた順に取り出して空にする。画面・ログ・終了コードへの
    // 通知は、呼び出し元ごとに作らず、この結果を受け取る1か所で行う。
    std::vector<FinishedLayoutResult> TakeFinishedResults();

private:
    // シーン操作の前の確認(読込中なら受け付けない)。
    bool PrepareSceneOperation() const;
    // ライブ状態の入れ替え後の後始末。切替で動いたrevisionを基準へ取り込み、markEditedなら未保存にする。
    void FinishSceneSwitch(bool markEdited);
    // 読込・保存・自動復元で同じ参照束を渡すため、組み立てを1箇所に集約する。
    RslPersistenceRefs MakePersistenceRefs(const std::vector<std::string>& pluginRootCandidates) const;

    // LoadLayout・NewLayoutの本体(結果を溜める処理は公開側が行う)。
    LayoutActionResult RunLoadLayout(const std::string& pathUtf8);
    LayoutActionResult RunNewLayout();
    // 正規化したレイアウトをライブへ反映し、文書の同一性(guid・保存先・表示名)を差し替えて選択を解除する。
    // 反映の途中で例外が出たら、文書を保存先なしの新しい文書にして(前のファイルを上書きさせない)再送出する。
    LayoutActionResult ApplyLayout(NormalizedLayout&& layout, const rs::world::Guid& documentGuid,
                                   const std::string& savedPathUtf8, const std::string& displayNameUtf8);

    // プラグイン参照を解決するルート候補(優先度順。設定のルートと実行ファイルのフォルダ)。
    std::vector<std::string> PluginRootCandidates() const;

    LayoutPersistenceRefs refs_;
    ScenePanelSource scenePanelSource_;
    std::vector<FinishedLayoutResult> finishedResults_;
    // 直近の削除1回分の控え。別のシーン操作・編集・レイアウトの読込で捨てる。
    std::optional<DeletedSceneBackup> deletedScene_;

    struct PendingLayoutLoad {
        std::string pathUtf8;
        bool indicatorShown = false;
        bool discardCurrentRecovery = false;
        bool restoring = false;
        std::string restoredSavedPathUtf8;
        std::string restoredDisplayNameUtf8;
    };
    std::optional<PendingLayoutLoad> pendingLoad_;
    // 予約の読込を適用したフレームの間だけ真。読込ジョブはこのフレームの後ろで始まるので、
    // それまでの間も IsLoading を真にする(予約は空で、ジョブはまだ動いていない隙間を埋める)。
    bool loadAppliedThisFrame_ = false;
};

}  // namespace rs::app
