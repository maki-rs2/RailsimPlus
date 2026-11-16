//
// SceneEntity.h - シーンに配置済みの1プラグイン分のデータ
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../alignment/Guid.h"
#include "TrainId.h"
#include "ScenePlacement.h"

namespace rs::world {

enum class SceneEntityCategory { TrackPlugin, Car, Scenery };

// PluginSelectorの候補リスト(まだ配置されていない記述子)とは別物。
// 「配置」操作で候補リストの1エントリからSceneEntityが1つ生成される。
struct SceneEntity {
    Guid guid = NilGuid();    // 個体の一意ID。SceneRegistryが発番するか、.rs2由来は住所から導出する
    std::string folderUtf8;   // プラグインフォルダパス(PluginSelectorの記述子と同じ形式)
    // 読む定義ファイルのファイル名(フォルダを含まない)。空なら既定の選び方(PickDefinitionFile、
    // *2k.txt優先)。同じフォルダに定義ファイルが複数あるとき、プラグインビューアーが開いたものを
    // 指定どおりに読むために使う。保存(.rsl)の対象は一時的な配置物だけが使うので、保存も読込もしない。
    std::string definitionFile;
    // .rs2由来の配置のみ非空。駅舎の接近判定マスクを番線表と結び付けるキー。
    std::string sourceAddress;
    SceneEntityCategory category = SceneEntityCategory::Scenery;
    ScenePlacement placement;
    uint64_t placementRevision = 0;
    bool reversed = false;    // 編成(Train)の反転フラグ。他カテゴリは常にfalse。
                               // ResolveWorldMatrix()には渡さず、配置生成側でTrackPlacement::
                               // yawOffsetDeg=180として反映する
    TrainId trainId = NilGuid();  // 編成: 同一編成に属する車両をグルーピングするID
    int carOrder = 0;     // 編成内の連結順(先頭=0)
    // placementのTrackPositionがどのSceneのgraph上のGuidを指しているか。編成(Train)専用: 編成はシーン間
    // ワープでアクティブSceneの外へ出られるため、座標だけでは所属Sceneが
    // 決まらない。他カテゴリは常にアクティブSceneの配置なので既定0のまま未使用。
    size_t placementSceneIndex = 0;
    // .rs2レイアウト等、配置元がスイッチ確定値を持つ場合にDefineSwitch宣言順で保持する
    // (rs::legacy_layout::ImportedPlacement::switchOptionsそのまま)。空なら全スイッチ既定0。
    std::vector<int32_t> switchOptions;
    // 信号プラグインのモデル(SignalModelSet が信号ごとに作る描画用の個体)か。SceneRegistry の個体は常に偽。
    // 真のとき、駅舎の実行時スイッチ(_APPROACH1/2・_STOPPING)を常に0とみなして静的判定する
    // (信号には列車の接近も停車も無い)。同じフォルダを駅として置いた個体とは、判定もバッチも別になる。
    bool isSignalModel = false;
    // 一時的な配置物か。プラグインビューアーで開いたものなど、レイアウトの一部ではない表示専用の個体。
    // 真のものは、保存・Undo・変更あり判定・Scene切替での全削除と全追加・ピッキング・
    // 設定ウィンドウのクリックの対象にしない。描画とスロット同期、実行時は通常の配置物と同じに扱う。
    bool isTemporary = false;
};

// レイアウトの一部としてユーザーが配置・編集・保存する個体か(一時的な配置物を除く)。
inline bool IsUserPlacement(const SceneEntity& entity) { return !entity.isTemporary; }

// 静的なGPUインスタンスバッチへ回してよい個体か。一時的な配置物は、時刻スイッチ・可動部・アニメ・
// 当たり判定の箱をスロットの毎フレームの反映で受けるため、静的に見えても必ずスロットへ載せる
// (バッチとスロットの両方に描かれて二重になるのを防ぐ。判定はスロット側とバッチ側でこの関数を共有する)。
inline bool MayUseStaticInstancing(const SceneEntity& entity) { return !entity.isTemporary; }

} // namespace rs::world
