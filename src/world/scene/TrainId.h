//
// TrainId.h - 編成(TrainGroup)を指す識別子の型エイリアス。呼び出し側はこの型名を
// 使うことで、識別子の型を変えるときの変更箇所を型定義1箇所に閉じ込める。
//
// SceneEntity.hではなくこの独立ヘッダーに置く理由: world/alignment層
// (PlatformRoutine.h/PointRoutine.h等、TrackGraph.hから使われる)がこの型を必要と
// するが、SceneEntity.hはScenePlacement.h経由でTrackTraversal.h/TrackGraph.hへ
// 依存しており、そちらを直接includeすると循環includeになるため。
//
#pragma once

#include "../alignment/Guid.h"

namespace rs::world {

// 編成(TrainGroup)を指す識別子。128bit GUID。
// 非編成センチネルはrs::world::NilGuid()。
using TrainId = Guid;

} // namespace rs::world
