#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../../platform/thread/WorkerThread.h"
#include "../locale/Localization.h"
#include "../workspace/ScenePanelPresentation.h"
#include "../plugin_session/LayoutSession.h"
#include "../plugin_session/SceneChoiceCatalog.h"

namespace rs::app {

struct SceneContentCounts {
    int warps = 0;
    int trains = 0;
};

// シーンごとの、ワープ(端に持つリンクの本数)と列車(編成の数)。第3段の削除確認の本文にも使う。
// シーンが空のセッションは、ライブの1シーン分を返す。
std::vector<SceneContentCounts> CountSceneContents(const LayoutSession& session,
                                                  const rs::world::SceneRegistry& registry);
// 一覧の行を作るための、選べる地形・環境の一覧と、シーンごとの地形の名前の控え。
// どちらも毎フレームは求めない(フォルダを読む・配置物を全件見るため)。
class ScenePanelSource {
public:
    ScenePanelSource();
    ~ScenePanelSource();
    ScenePanelSource(const ScenePanelSource&) = delete;
    ScenePanelSource& operator=(const ScenePanelSource&) = delete;

    // 選べる地形・環境の走査をワーカーへ頼む(パネルを開いたときに呼ぶ)。プラグインのフォルダを全部開くので、
    // 描画スレッドでは走らせない。結果は PollChoices が受け取る。
    void Rescan(const std::vector<std::string>& rootCandidatesUtf8);
    // 走査の結果が届いていれば取り込む。取り込んだらtrue。
    bool PollChoices();
    // 頼んだことがあるか(結果が届いていなくてもtrue)。
    bool Scanned() const { return scanned_; }
    // 結果が1回でも届いているか。届くまでの Choices() は空で、「選べない」の意味ではない。
    bool ChoicesReady() const { return choicesReady_; }
    const SceneChoiceCatalog& Choices() const { return choices_; }
    // シーンごとの地形の名前(無ければ空。要素数はシーン数。シーンが空のセッションは1つ)。今のシーンはライブの
    // 配置物から、ほかのシーンはキャッシュから求める。地形は追加後に変わらないので、シーンの並び・配置物の数が
    // 変わったときだけ求め直す。
    const std::vector<std::string>& SurfaceNames(const LayoutSession& session, const rs::world::SceneRegistry& registry);

private:
    using ScanWorker = rs::platform_thread::WorkerThread<std::vector<std::string>, SceneChoiceCatalog>;
    SceneChoiceCatalog choices_;
    bool scanned_ = false;
    bool choicesReady_ = false;
    std::unique_ptr<ScanWorker> worker_; // 初めて頼むときに起動する
    // 求め直す必要があるかを見るための、前回の状態(今のシーン番号・ライブの配置物の数・シーンごとの配置物の入れ物)。
    struct Signature {
        size_t active = 0;
        size_t liveCount = 0;
        std::vector<std::pair<const void*, size_t>> scenes;
        bool operator==(const Signature&) const = default;
    };
    Signature signature_;
    bool hasNames_ = false;
    std::vector<std::string> surfaceNames_;
};

// フォルダが地形プラグインなら、その名前(フォルダ名)を返す。違えば空。
std::string SurfaceNameOfFolder(const std::string& folderUtf8);
// シーンの配置物のうち、最初の地形の名前。無ければ空。
std::string SurfaceNameOfEntities(const std::vector<rs::world::SceneEntity>& entities);

// 環境のプルダウンの選択肢を作る。先頭から、(現在値が選べないときだけ)選べない現在値、デフォルトの空、
// キューブ付きの環境の順。selectedは現在値に当たる選択肢の添字。現在値の綴りは大文字小文字を区別せずに比べる。
std::vector<workspace::SceneEnvOption> BuildEnvOptions(const std::string& currentEnv,
                                                       const std::vector<SceneChoice>& envs, size_t& selected);

// 削除の確認ダイアログの本文。ワープも列車も無いときは、「一緒に消えます」の部分を付けない。
std::string BuildSceneDeleteMessage(const Localization& localization, const std::string& sceneName, int warps,
                                    int trains);

// 一覧の行を作り直す。編集中・下書きは今の行から引き継ぐ。変わったときだけtrue。
bool FillScenePanelFromSession(workspace::ScenePanelPresentation& panel, const LayoutSession& session,
                               const rs::world::SceneRegistry& registry, ScenePanelSource& source);

} // namespace rs::app
