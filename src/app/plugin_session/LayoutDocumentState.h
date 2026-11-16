//
// LayoutDocumentState.h - レイアウト文書の同一性(GUID)と未保存状態の保持。
//
// dirtyはオーサリング編集だけを対象にする。
// シミュレーション進行は毎フレーム状態を変えるため、判定材料に含めない。判定は編集で増える
// 既存のリビジョンカウンタ(Editor::revision / SceneRegistry::PersistentRevision())の組を基準値と比較する
// 方式にして、編集経路ごとの通知呼び出しを不要にする。どちらのカウンタにも現れない編集
// (SceneRenderer側のレジストリ操作)だけがMarkEditedを使う。
//
#pragma once

#include <cstdint>
#include <string>

#include "../../world/alignment/Guid.h"

namespace rs::app {

struct LayoutDocumentBaseline {
    uint64_t editorRevision = 0;
    uint64_t sceneRegistryRevision = 0;
    uint64_t markedEdits = 0;
};

struct LayoutDocumentState {
    // 未保存の新規文書として始めるため、既定構築の時点で採番する。
    rs::world::Guid guid = rs::world::GenerateGuid();
    // 正規保存先。読取り専用形式を読み込んだ直後と新規は空(=未保存扱い)。
    std::string savedPathUtf8;
    // 保存先を持たない文書の表示名(.rs2を読み込んだときのファイル名など)。無ければ空。
    std::string displayNameUtf8;
    LayoutDocumentBaseline baseline;
    uint64_t markedEdits = 0;

    void MarkEdited();
    bool IsDirty(uint64_t editorRevision, uint64_t sceneRegistryRevision) const;
    // 編集でない契機(読込・保存・Scene切替)で基準値を取り直す。
    void Rebase(uint64_t editorRevision, uint64_t sceneRegistryRevision);
};

}  // namespace rs::app
