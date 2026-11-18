//
// EditorSectionEdit.h - edge内区間分割(pluginSections)の編集操作。
//
// EditorEdgeProperties.h と同じ規約(Editorと対象edge guidを明示引数で
// 受け取る無状態の操作クラス)。UI(パネル・ビューポート描画・マウス操作)は
// このクラスの責務外。
//
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../../world/scene/ManualPlacementOverlay.h"
#include "Command.h"
#include "EditorTypes.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackEdge;
} // namespace rs::world

namespace rs::editor {

class Editor;

class EditorSectionEdit {
public:
    // sの位置に境界を1本入れる。前後の値は同じまま(=呼び出し側が直後に片側の値を編集する前提)。
    // 既存境界と kMinSpanLen 未満で一致する場合や s<=0/s>=edge全長 の場合は false。
    bool SplitSectionAt(Editor& editor,
                        const rs::world::Guid& edge, SectionLayer layer, double s);

    // boundaryIndex 番目の境界を削除する(前の区間が後ろを吸収する)。
    // boundaryIndex は「境界の番号」= Spans() の添字で 1 以上(0番目のSpanのsStartは
    // 常に0で、これはedge先頭であり境界ではないので削除できない)。範囲外なら false。
    bool RemoveSectionBoundary(Editor& editor,
                               const rs::world::Guid& edge, SectionLayer layer,
                               std::size_t boundaryIndex);

    // boundaryIndex 番目の境界を newS へ動かす。両隣の境界の間に kMinSpanLen 以上の
    // 余裕を残すようクランプする(隣を追い越したり潰したりしない)。
    // クランプ後の位置が元と kMinSpanLen 未満しか違わないなら false(no-op)。
    bool MoveSectionBoundary(Editor& editor,
                             const rs::world::Guid& edge, SectionLayer layer,
                             std::size_t boundaryIndex, double newS);

    // sectionIndex 番目の区間の値を設定する(層ごとに1つ)。範囲外なら false。
    // 区間のfolderセットを丸ごと差し替えるAPIなので、UI個別に直すと書き戻し経路が
    // 残る。実体化済みfolderの再投入は解除操作へ集約する。
    bool SetSectionFolders(Editor& editor,
                           const rs::world::Guid& edge, std::size_t sectionIndex,
                           std::vector<std::string> folders,
                           const rs::world::ManualPlacementOverlay& overlay);

    // edgesの全区間から、removeのフォルダを外し、addのフォルダを(無い区間にだけ)加える。
    // 区間の境界は変えず、全体を1回のUndoにする。複線中心線(centerlineOnly)は飛ばす。
    // 加えるフォルダに実体化済みの配置(runAnchors)があるedgeが1本でもあれば、何も変えずfalse。
    // 何も変わらないときもfalse。
    bool EditFoldersOnEdges(Editor& editor,
                            const std::vector<rs::world::Guid>& edges,
                            const std::vector<std::string>& add,
                            const std::vector<std::string>& remove,
                            const rs::world::ManualPlacementOverlay& overlay);

    // s を含む区間の添字を返す(始点包含)。UIが「クリックした位置の区間」を選ぶのに使う。
    static std::size_t SectionIndexAt(const rs::world::TrackEdge& edge, SectionLayer layer,
                                      double s);
    // layer 番目の区間数。
    static std::size_t SectionCount(const rs::world::TrackEdge& edge, SectionLayer layer);

    // 境界(index>=1、Spans()の添字)のs値だけを列挙する(添字0の区間開始s=0は境界
    // ではないため含めない)。世界座標への変換・投影・ピッキングはapp/ui側
    // (ViewportSectionOverlay)が担う。ここはグラフ/ImGui非依存の純粋関数として
    // ヘッドレステストできる。
    struct BoundaryS {
        std::size_t index = 0;
        double s = 0.0;
    };
    static std::vector<BoundaryS> CollectBoundaries(const rs::world::TrackEdge& edge,
                                                     SectionLayer layer);
};

} // namespace rs::editor
