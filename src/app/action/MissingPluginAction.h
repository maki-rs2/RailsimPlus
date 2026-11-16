//
// MissingPluginAction.h - 見つからないプラグインの一覧と、差し替え・削除の操作
//
// 一覧(Collect)は、レイアウトが参照しているのにフォルダが無いプラグインを、場所の種類ごとに数える。
// 差し替え(Replace)は、一覧の1行が指す参照を全部、別のプラグインへ置き換える。置き換えは
// 1回のUndoで戻る。削除(Delete)は、一覧の1行が指す参照を参照元ごと全部消す。これも1回のUndoで戻る。
// ImGui版とRmlUi版の画面が同じ操作を呼ぶ。
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "ActionError.h"
#include "../plugin_session/MissingPluginCatalog.h"
#include "../../plugins/schema/PluginSchema.h"

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class PluginSelector;
struct ImportedWireSpan;
struct LayoutSession;

// 失敗の分類。表示文言はstatusとdetailからUIの端で組み立てる。
enum class MissingPluginReplaceStatus {
    Ok,
    NothingToReplace,  // 行が指す参照がもう無い(一覧が古い)
    SameAsSource,      // 差し替え先が元と同じフォルダ
    LoadFailed,        // 差し替え先を読めなかった(detailに理由)
    TypeMismatch,      // 差し替え先の種類がその場所に合わない(detailに種類名または理由)
    EnvOutsideRoots,   // 環境プラグインが、どのプラグインルートの Env/ 直下でもない
    NotDeletable,      // その場所の参照は削除できない(環境。Deleteだけが返す)
};

ErrorSeverity SeverityOf(MissingPluginReplaceStatus status);

struct MissingPluginReplaceResult {
    MissingPluginReplaceStatus status = MissingPluginReplaceStatus::Ok;
    std::string detail;
    std::size_t replacedCount = 0;  // 書き換えた(Deleteでは削除した)参照の数(Okのとき)
    bool Ok() const { return status == MissingPluginReplaceStatus::Ok; }
};

// 実体への参照の束。importedWireSpansは複製せず、アプリが持つ配列そのものを渡す。
struct MissingPluginRefs {
    rs::editor::Editor& editor;
    rs::world::SceneRegistry& sceneRegistry;
    PluginSelector& selector;
    LayoutSession& session;
    std::vector<ImportedWireSpan>& importedWireSpans;
    // プラグインルート候補(優先度順)の供給元。環境プラグインの参照先を組むのに使う
    // (ActiveEnv::Updateに渡すものと同じ)。
    std::function<std::vector<std::string>()> pluginRoots;
    // 差し替え先の種類の判定。空ならDefaultClassifyPluginType(定義ファイルを読む)。
    std::function<rs::legacy_pi::PluginType(const std::string& folderUtf8)> classifyPluginType;
    // フォルダの有無の判定。空ならPluginFolderExists。
    PluginExistsFn folderExists;
};

class MissingPluginAction {
public:
    explicit MissingPluginAction(const MissingPluginRefs& refs) : refs_(refs) {}

    // 見つからないプラグインの一覧(フォルダのキーの昇順、次に場所の種類の順)。
    std::vector<MissingPluginEntry> Collect() const;

    // rowが指す参照(同じフォルダ表記のキー×場所の種類)を全部、toFolderUtf8のプラグインに差し替える。
    // 検査(種類・読込)に通らなければ、何も変えずに理由を返す。スイッチ値は既定(空)に戻る。
    MissingPluginReplaceResult Replace(const MissingPluginEntry& row, const std::string& toFolderUtf8);

    // rowが指す参照を全部、参照元ごと削除する(個体・信号・架線柱・橋脚・架線・編成の車は消え、
    // レール区間はレールを残してそのフォルダだけが外れる)。環境は削除できず、何も変えずにNotDeletableを返す。
    MissingPluginReplaceResult Delete(const MissingPluginEntry& row);

private:
    // rowが指す参照(同じフォルダ表記のキー×場所の種類)の数。
    std::size_t CountReferences(const MissingPluginEntry& row, const std::vector<std::string>& roots) const;

    MissingPluginRefs refs_;
};

}  // namespace rs::app
