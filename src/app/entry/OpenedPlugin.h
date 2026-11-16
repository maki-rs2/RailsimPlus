//
// OpenedPlugin.h - 起動時に開いたプラグイン(ビューアー・--openplugin)を情景スロットで出す
//
// 開いたプラグインは、原点に置く一時的な配置物(SceneEntity::isTemporary)としてSceneRegistryへ登録し、
// 本体と同じ情景スロットの経路で読込・反映・描画する。
// アセンブリを持たないプラグイン(押し出し表示・パーツだけのプラグイン)は、表示用のメッシュを作って
// 配置物の表示メッシュとして渡す(SceneRenderer::SetExternalMesh)。定義ファイルの指定は、
// 配置物の定義ファイル名(SceneEntity::definitionFile)に入れる。
//
#pragma once

#include <cstdint>
#include <string>

#include "../../world/alignment/Guid.h"

namespace rs::app {

struct AppRuntimeGlobals;
struct AppRuntimeState;
struct PluginSession;

struct OpenedPluginState {
    enum class Mode {
        None,
        Slot, // 一時的な配置物としてスロットで出している
    };
    Mode mode = Mode::None;
    // Slotのときの一時的な配置物のid。
    rs::world::Guid entityId = rs::world::NilGuid();
    // 開いたフォルダ。
    std::string folderUtf8;
    // 取り付け(スイッチの初期値・フォントの再構築)を済ませたスロットの印。個体のidとスロットの世代の組が
    // 変わったら(再読込・スロットの入れ替え)取り付け直す。
    rs::world::Guid attachedEntityId = rs::world::NilGuid();
    uint64_t attachedGeneration = 0;
    // 最初のカメラ合わせを済ませたか(スロットの当たり判定の箱が決まるまで待つ)。
    bool cameraFitted = false;
    // 最初のカメラを合わせる範囲を、スロットの当たり判定の箱でなく指定するか(押し出し表示は中央の短い区間)。
    bool hasFocusBounds = false;
    float focusMin[3] = {0.0f, 0.0f, 0.0f};
    float focusMax[3] = {0.0f, 0.0f, 0.0f};
    // スロットに載せられなかった警告を出したか(1回だけ出す)。
    bool failureReported = false;
    // 稀な漢字を含めてフォントアトラスを組み直す要求(RunPluginTickAndFontRebuildが消す)。
    bool fontRebuildNeeded = false;
};

// プラグインフォルダを開く。前に開いていたものは外す。
void OpenPlugin(AppRuntimeState& state, const std::string& folderUtf8);

// 開いているプラグインの実行時セッション(読込中や未読込ならnullptr)。スロットは中身が入れ替わるので、
// 保持せず使うたびに引き直す。
PluginSession* FindOpenedPluginSession(AppRuntimeState& state);

// 毎フレーム、スロットの同期(Sync)の後・Tickの前に呼ぶ。読込の完了待ち・読込失敗の警告・
// 取り付け(最初の1回)・ビューアーのカメラ合わせを行う。
void TickOpenedPlugin(AppRuntimeGlobals& globals, AppRuntimeState& state);

} // namespace rs::app
