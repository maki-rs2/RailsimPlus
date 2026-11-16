//
// MissingPluginRefreshGate.h - 見つからないプラグインの一覧を、いつ作り直すかの判定
//
// 一覧の作成(MissingPluginAction::Collect)は全参照を歩くので、毎フレームは呼ばない。レイアウトの
// 中身が変わったことを表す値の組(MissingPluginSignature)を毎フレーム渡してもらい、変わったときだけ
// 作り直す。ノードのドラッグなどで値が毎フレーム変わり続けている間は待ち、変化が止まって
// kSettleSecondsたってから1回だけ作り直す。自分の操作(差し替え・削除・再走査・ルート変更)のあとは
// Forceで、次のUpdateですぐ作り直す。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
}

namespace rs::app {

class PluginSelector;
struct LayoutSession;

// 一覧の中身が変わるかもしれない状態の写し。
struct MissingPluginSignature {
    std::uint64_t editorRevision = 0;     // 敷設・Undo/Redo・Scene切替・レイアウト読込(Editor::revision)
    std::uint64_t registryRevision = 0;   // 保存対象の配置物の増減(SceneRegistry::PersistentRevision)
    std::size_t activeScene = 0;
    std::size_t sceneCount = 0;
    // 編成の車の増減(編集はrevisionに出ないため、数で見る)。
    std::size_t formationCount = 0;
    std::size_t carCount = 0;
    std::size_t unresolvedCarCount = 0;
    std::uint64_t rootsHash = 0;          // プラグインルートの並び
    bool operator==(const MissingPluginSignature&) const = default;
};

MissingPluginSignature MakeMissingPluginSignature(const rs::editor::Editor& editor,
                                                  const rs::world::SceneRegistry& sceneRegistry,
                                                  const LayoutSession& session, const PluginSelector& selector,
                                                  const std::vector<std::string>& pluginRoots);

class MissingPluginRefreshGate {
public:
    // 値が変わってから作り直すまでの待ち[秒]。
    static constexpr double kSettleSeconds = 0.3;

    // 今フレームで作り直すならtrue。最初の呼び出しは必ずtrue。
    bool Update(const MissingPluginSignature& signature, double deltaSeconds);
    // 次のUpdateで必ずtrueにする。
    void Force() { forced_ = true; }

private:
    bool initialized_ = false;
    bool forced_ = false;
    bool pending_ = false;
    double sinceChange_ = 0.0;
    MissingPluginSignature last_;
};

}  // namespace rs::app
