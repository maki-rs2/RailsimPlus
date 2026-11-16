//
// SwitchListWidget.h - スイッチ一覧のImGui描画(単体表示・配置個体で共用)
//
#pragma once

#include <vector>

#include "../action/SwitchList.h"

namespace rs::app {

// rowsをスクロール枠の中に並べ、変更があればwriter.SetSwitchへ行の添字つきで書き込む。
// 選択肢のあるスイッチはコンボ、無いスイッチは見出しか空き(GetSwitchRowKind)。
// 見出しや「スイッチなし」の表示は呼び出し側で行う。
// linkedToBlockの行は編集させず、名前を灰色にして「閉塞に連動中」を横に出す。
void DrawSwitchList(const char* childId, const std::vector<SwitchRowViewModel>& rows,
                    SwitchValueWriter& writer);

} // namespace rs::app
