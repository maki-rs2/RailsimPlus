//
// PlatformStopInfo.h - 駅と番線(停止位置とルーチン)
//
// 駅・番線だけを使う側が、軌道の形の定義まで読まずに済むよう TrackGraph.h から分けてある。
//
#pragma once

#include <string>
#include <vector>

#include "Guid.h"
#include "PlatformRoutine.h"

namespace rs::world {

// 駅。Undo/Redoのコマンドの約束事(ICommand::apply/revertがTrackGraphしか受け取らない)で
// 復元できるよう、番線と同じTrackGraphに置く。番線はstationIdで駅を指す一方向参照で、
// 駅側にplatforms[]の逆一覧は持たない(両方向を保存するとUndoが整合させる対象が増える)。
struct StationInfo {
    Guid id = NilGuid();
    std::string name;           // 駅名(UTF-8)
    std::string sourceAddress;  // 配置との照合に使うAddress。手動作成駅では空。
};

// 停止位置の基準。向きは進行方向で決まる(番線チェーンが進行方向で解決されるため)。
// Front=編成先頭を進行方向側の番線端へ、Center=編成中央を番線中央へ、
// Rear=編成後端を進行方向手前側の番線端へ揃える。正規化Offsetへの変換は
// PlatformChain.h の ResolveStopOffset が行う。
enum class StopReference { Front, Center, Rear };

// 駅の番線(プラットフォーム)。railEdgesが解決できたものを保持する。接近判定では
// stoppable=false の番線も必要になる。id はRailSim II 側の Platform Addressとは無関係の
// 内部識別子(インポート時に新規発行)。
struct PlatformStopInfo {
    Guid id = NilGuid();
    Guid stationId = NilGuid();  // 所属駅。解決できない番線はNilGuidのまま保持する。
    std::string name;            // 番線名(UTF-8)
    bool stoppable = true;
    // 全列車共通の停止位置設定。個別・タグの設定層が追加されても、この既定層は残る。
    StopReference stopReference = StopReference::Center;
    double stopOffsetM = 0.0;
    std::vector<PlatformRoutineElement> routine;
    std::vector<PlatformTrainRoutine> trainRoutines;
    // 配置前の編成定義(Formation)に紐付くルーチン。編成が配置された時点で
    // trainRoutinesへ複写される(走行系はこちらを直接読まない)。
    std::vector<PlatformFormationRoutine> formationRoutines;
    // List order is tag-layer priority. UI must not sort this collection.
    std::vector<PlatformTagRoutine> tagRoutines;
    std::string stationAddress;  // この番線が属する駅舎配置の RailSim II の Address
    // この番線が属する駅の配置(SceneEntity::guid)。エディタが施設の Platform から作った番線だけ持つ
    // (.rs2取り込みは stationAddress で結ぶ)。Nil=不明。
    Guid stationEntity = NilGuid();
    int indexInStation = 0;      // 駅舎内のPlatform出現順(接近マスクのビット位置)
    std::vector<Guid> railEdges;  // このプラットフォームに属するedge(RailList解決済み)
    // RailSim II の`OpenDoor = yes/no, yes/no;`(openDoor[0]=左、openDoor[1]=右)。
    // _DOOR1/_DOOR2への割り当ては読み出し側(SceneRenderer::TickTrainGroups)で
    // openDoor[1]→_DOOR1、openDoor[0]→_DOOR2と反転させている(ユーザー指定)。
    bool openDoor[2] = {false, false};
    // どちらかのドアを開けるか(ドアを閉じ始める先行時間の有無を決める)。
    bool OpensAnyDoor() const { return openDoor[0] || openDoor[1]; }
};

} // namespace rs::world
