//
// PluginReferenceDelete.h - 見つからないプラグインを使う参照を、参照元ごと全部削除する
//
// 削除(DeletePluginReferences)とその取り消し(RestoreDeletedPluginReferences)。どちらも
// PluginReferenceWalk.h の訪問で参照を歩く。削除する前の要素は丸ごと控え、取り消しで元の位置へ戻す。
// 個体のguidと並び順は変わらない。
//
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "PluginReferenceReplace.h"
#include "PluginReferenceWalk.h"
#include "PluginSelector.h"
#include "Rs2SceneApplier.h"
#include "../../editor/tool/Command.h"
#include "../../world/alignment/Guid.h"
#include "../../world/scene/ManualPlacementOverlay.h"
#include "../../world/scene/SceneEntity.h"

namespace rs::app {

// 削除で消えた要素の、削除前の状態(取り消し用)。
struct PluginDeleteLog {
    // 個体(情景・車両)。indexは、レジストリのEntities()か、別Sceneのキャッシュのmanual配置の中での位置。
    struct EntityBackup {
        bool inRegistry = true;
        std::size_t sceneIndex = 0;  // inRegistryがfalseのとき、キャッシュのScene
        std::size_t index = 0;
        rs::world::SceneEntity entity;
    };
    struct CarBackup {
        std::size_t formationIndex = 0;
        std::size_t carIndex = 0;
        FormationCar car;
    };
    // Scene1つぶんの、信号・オーバーレイ・架線スパンの並び。削除で数が減ったものだけ控える。
    struct SceneBackup {
        std::size_t sceneIndex = 0;
        std::optional<std::vector<rs::world::SpeedSignal>> signals;
        std::optional<std::vector<rs::world::PlacedIndividual>> poles;
        std::optional<std::vector<rs::world::PlacedIndividual>> piers;
        std::optional<std::vector<rs::world::WireSpan>> lines;
        std::optional<std::vector<ImportedWireSpan>> wireSpans;
    };

    // 削除した順(訪問順)に並ぶ。
    std::vector<EntityBackup> entities;
    // 番線とアンカーの対応を削除・復元する共通コマンド。
    std::vector<std::unique_ptr<rs::editor::ICommand>> anchors;
    std::vector<CarBackup> cars;
    // レール区間は、削除の前の区間ごと控える。
    std::vector<PluginReplaceLog::EdgeBackup> edges;
    std::vector<SceneBackup> scenes;
};

// siteの場所で、folderUtf8(PluginFolderKeyで比べる)を使う参照を全部削除し、削除した参照の数を返す。
// ・個体は、レジストリ(別Sceneはキャッシュ)から消す。駅舎の個体は、持っていた番線も消す。
// ・レール区間は、レールを残してそのフォルダだけを区間から外す(区間の境界は変えない)。
// ・架線柱・橋脚は、つながる架線も一緒に消える。
// ・Environmentは削除しない(0を返す)。
// logは中身を捨てて、取り消しに要る情報を入れる。
size_t DeletePluginReferences(PluginReferenceContext& context, PluginReferenceSite site,
                              const std::string& folderUtf8, PluginDeleteLog& log);

// DeletePluginReferencesの直後(間にほかの変更が無い状態)に呼び、削除前に戻す。
void RestoreDeletedPluginReferences(PluginReferenceContext& context, const PluginDeleteLog& log);

}  // namespace rs::app
