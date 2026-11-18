//
// DiaPanelPresentationBuilder.h - ダイヤモードの左タブの表示値を、軌道データ(TrackGraph)から作る
//
#pragma once

#include <string>
#include <vector>

#include "../workspace/DiaPanelPresentation.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackGraph;
} // namespace rs::world

namespace rs::app {

struct DiaPanelSceneSource {
    std::string name;
    // アクティブなシーンはライブのグラフ(Editor::graph)を指す。キャッシュの写しは書き戻し前で古い。
    const rs::world::TrackGraph* graph = nullptr;
};

struct DiaPanelFormationName {
    rs::world::Guid id = rs::world::NilGuid();
    std::string name;
};

struct DiaPanelTagName {
    int id = 0;
    std::string label;
};

struct DiaPanelBuildInput {
    // 1つ以上。シーンが1つのときは、シーン名の行を作らず、駅を最上位に並べる。
    std::vector<DiaPanelSceneSource> scenes;
    int activeScene = 0;
    std::vector<DiaPanelFormationName> formations;
    std::vector<DiaPanelTagName> tags;
};

// 全シーンの駅と未所属の番線を、シーン > (駅 | 未所属の番線) > 駅の番線 の順に並べた表示値を作る。
// 画面は一覧(screen_ = 0)で、開いている番線・層・要素はない(CarryOverDiaPanelState で引き継ぐ)。
workspace::DiaPanelPresentation BuildDiaPanelPresentation(const DiaPanelBuildInput& input);

// 作り直した表示値 next へ、previous の画面・開いている番線/層/要素・畳んだ行・名前の入力中の状態を、
// Guid と層のキーで引き継ぐ。対象が消えていたら、その一つ上の画面へ戻す。ホバーは引き継がない。
void CarryOverDiaPanelState(const workspace::DiaPanelPresentation& previous, workspace::DiaPanelPresentation& next);

} // namespace rs::app
