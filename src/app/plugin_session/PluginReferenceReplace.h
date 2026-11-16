//
// PluginReferenceReplace.h - 見つからないプラグインの参照を、別のプラグインへ全部差し替える
//
// 差し替え(ReplacePluginReferences)とその取り消し(RestorePluginReferences)。どちらも
// PluginReferenceWalk.h の訪問で参照を歩く。差し替え先の読込と種類の検査は呼び出し側が
// 済ませ、結果を PluginReplacePlan で渡す(この層はファイルを読まない)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "PluginReferenceWalk.h"
#include "PluginSelector.h"
#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/Guid.h"
#include "../../world/alignment/IntervalList.h"
#include "../../world/alignment/TrackGraph.h"

namespace rs::app {

// siteの場所に置けるプラグインの種類か。
bool SiteAcceptsPluginType(PluginReferenceSite site, rs::legacy_pi::PluginType type);

// 準備(差し替え先の読込と種類の検査)が済んだ差し替え。
struct PluginReplacePlan {
    PluginReferenceSite site = PluginReferenceSite::SceneryEntity;
    // 差し替える参照のフォルダ。PluginFolderKeyで比べるので、表記は何でもよい。
    std::string fromFolderUtf8;
    // 参照に書き込む差し替え先の表記。
    std::string toFolderUtf8;
    // 編成の車のとき、差し替え先を読んで作った1両(寸法などの写し元)。
    std::optional<FormationCar> car;
};

// 差し替えで書き換えた場所の、変更前の状態(取り消し用)。
struct PluginReplaceLog {
    // 訪問の通し番号で指す場所の、変更前の値。
    struct Item {
        size_t ordinal = 0;
        std::string folderUtf8;
        std::optional<std::vector<int32_t>> switchOptions;
        std::optional<FormationCar> car;
    };
    // レール区間は、重複を除くと区間内の要素数が変わり、通し番号では戻せないので区間ごと控える。
    struct EdgeBackup {
        size_t sceneIndex = 0;
        rs::world::Guid edge = rs::world::NilGuid();
        rs::world::IntervalList<rs::world::PluginSet> sections;
    };
    // 環境は、名前とフォルダの組で控える(フォルダが空だった状態に戻すため)。
    struct EnvBackup {
        size_t sceneIndex = 0;
        std::string envPlugin;
        std::string envFolderUtf8;
    };
    std::vector<Item> items;
    std::vector<EdgeBackup> edges;
    std::vector<EnvBackup> environments;
    // 差し替えた後のフォルダのキー。取り消しで、書き戻す場所がまだ差し替え後の値か確かめる。
    std::string appliedKey;
};

// slotがTrackSectionのとき、そのedgeの区間をedgesへ控える(同じedgeを続けて訪ねたときは1回だけ)。
void RecordEdgeBackup(std::vector<PluginReplaceLog::EdgeBackup>& edges, const PluginReferenceSlot& slot);
// RecordEdgeBackupで控えた区間を、元のedgeへ書き戻す。
void RestoreEdgeBackups(PluginReferenceContext& context,
                        const std::vector<PluginReplaceLog::EdgeBackup>& edges);

// planに当たる参照を全部書き換え、書き換えた数を返す。スイッチ値は空(既定)にする。
// logがnullptrでなければ、中身を捨てて、取り消しに要る情報を入れる。
size_t ReplacePluginReferences(PluginReferenceContext& context, const PluginReplacePlan& plan,
                               PluginReplaceLog* log);

// ReplacePluginReferencesの直後(間にほかの変更が無い状態)に呼び、変更前に戻す。
void RestorePluginReferences(PluginReferenceContext& context, const PluginReplaceLog& log);

}  // namespace rs::app
