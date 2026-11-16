//
// SceneRegistry.h - シーンに配置済みのSceneEntity一覧
//
// GPU非常駐の純データコンテナ。GPU常駐・描画は持たず、描画側がEntities()との
// 差分を見て担当する。
//
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "SceneEntity.h"

namespace rs::world {

class SceneRegistry {
public:
    // 追加。guidを新しく発番して設定し、返す。
    Guid AddEntity(SceneEntity entity);

    // entityのguidを保持したままレジストリへ戻す。Scene切替時の手動配置復元と
    // .rsl読込時の復元、.rs2由来の配置(住所から導出したguid)に使う。
    // guidが未設定なら発番する。既に同じguidの個体がいるときは、衝突しないよう振り直す。
    Guid RestoreEntity(SceneEntity entity);

    void RemoveEntity(const Guid& guid);

    // entityのguidを保持したまま、Entities()のindex番目へ挿入する(削除の取り消し用)。
    // indexが大きすぎれば末尾へ入れる。guidが未設定、または同じguidの個体がいるときは発番し直す。
    Guid RestoreEntityAt(size_t index, SceneEntity entity);

    // 以下の一括削除は、一時的な配置物(isTemporary)を消さない。
    // category に一致する全エンティティを一括削除する(.rs2再読込時に前回分の
    // Scenery配置を残さず入れ替えるためのもの、RemoveEntityのループ呼び出しだと
    // revisionが配置数分増えてしまうため専用関数にする)。
    void RemoveByCategory(SceneEntityCategory category);

    // 既存エンティティの設置座標だけを更新する(guid不一致時は何もしない)。
    void UpdatePlacement(const Guid& guid, ScenePlacement placement);

    // ユーザー確定の設置座標変更。既存の毎フレーム更新とは異なり、描画バッチと保存状態を更新する。
    void CommitPlacement(const Guid& guid, ScenePlacement placement);


    // 設置座標と「その座標が属するSceneの添字」を同時に更新する(編成向け)。シーン間ワープで所属Sceneが変わる
    // 編成は、TrackPositionとplacementSceneIndexを必ず一組で書き戻す必要がある
    // (別々に書くと1フレームだけ別Sceneのgraphに対して解決される)。
    // UpdatePlacementと同じくrevision_は増やさない。
    void UpdatePlacementWithScene(const Guid& guid, ScenePlacement placement,
                                  size_t placementSceneIndex);

    // 配置に焼き込まれているスイッチ値のうち1つだけを書き換える(guid不一致時は何もしない)。
    // switchOptionsが短ければ0埋めで伸ばす。値が変わらなければ何もしない(revisionも増やさない)。
    // 変化したときだけrevision_を増やし、シーン側の再ロード/再バッチを促す。
    // 変更経路をこの1関数に集約しておくのは、後からUndoコマンドで包めるようにするため。
    void SetSwitchOption(const Guid& guid, size_t index, int32_t value);

    // 運行に合わせた表示(行先・種別など)のように、保存しない一時的なスイッチ値を書く。
    // 描画は保存値と同じく switchOptions を見るが、保存値は別に控えるのでPersistentRevision()は進まず、
    // 保存には SavedSwitchOptions() の値が出る。ClearRuntimeSwitchOptions() で保存値へ戻す。
    void SetRuntimeSwitchOption(const Guid& guid, size_t index, int32_t value);
    void ClearRuntimeSwitchOptions(const Guid& guid);
    // 保存すべきスイッチ値(一時的な値が書かれていれば、その前の保存値)。
    const std::vector<int32_t>& SavedSwitchOptions(const SceneEntity& entity) const;

    // 既存エンティティの参照先プラグインを差し替え、スイッチ確定値も渡された値にする(guid不一致時は何もしない)。
    // 構成の変化として扱い、静的バッチとスロットの作り直しを促す。保存対象の配置物ならPersistentRevision()も進む。
    void ReplaceEntityPlugin(const Guid& guid, std::string folderUtf8, std::vector<int32_t> switchOptions);

    const std::vector<SceneEntity>& Entities() const { return entities_; }

    // guidに一致するエンティティを探す(無ければnullptr)。
    const SceneEntity* FindEntity(const Guid& guid) const;

    // 構成(エンティティの追加/削除/置換)が変化するたびに増える世代カウンタ。
    // UpdatePlacement(既存エンティティの座標だけの更新、走行中編成で毎フレーム
    // 呼ばれる)では増やさない。GPUインスタンスバッチ再構築(EditorViewport.cppの
    // needRebuild判定)を「毎フレーム全走査」ではなく「構成が変わった時だけ」に
    // 絞るためのトリガーとして使う。
    uint64_t Revision() const { return revision_; }

    // Revision()と同じ箇所で増えるが、SetSwitchOptionでは増えない世代カウンタ。
    // 「配置の増減・置換」と「スイッチ値だけの変更」を区別したい側(静的Sceneryの
    // インスタンスバッチ更新)が、前者のときだけ全再構築するために使う。
    uint64_t StructureRevision() const { return structureRevision_; }

    // Revision()のうち、保存対象の配置物(一時的な配置物を除く)の変化だけで増える世代カウンタ。
    // 追加・削除・置換・スイッチ値の変更のどれでも、一時的な配置物だけが変わったときは増えない。
    // レイアウトの変更あり判定(LayoutDocumentState)にはこちらを渡す。
    uint64_t PersistentRevision() const { return persistentRevision_; }

    // SetSwitchOptionが値を変えた個体と、その変更より前のswitchOptions。
    // 同じ個体が複数回変わったときは、最初の変更前の値だけを残す。
    struct SwitchChange {
        Guid entityId;
        std::vector<int32_t> previousSwitchOptions;
    };

    // 溜まっているスイッチ変更を取り出して空にする。
    std::vector<SwitchChange> TakePendingSwitchChanges();

    struct PlacementChange {
        Guid entityId;
        ScenePlacement previousPlacement;
    };
    std::vector<PlacementChange> TakePendingPlacementChanges();

private:
    // entities_全走査でidToIndex_を再構築する(削除等でインデックスが全ずれした時専用、
    // 削除は稀なので毎回全再構築で良い)。
    void RebuildIndex();

    // revision_とstructureRevision_を一緒に進める(SetSwitchOption以外の変更用)。
    // persistentが真(保存対象の配置物が変わった)のときだけpersistentRevision_も進める。
    void MarkStructureChanged(bool persistent);

    // 一時的な配置物を除き、matchesに当たる個体を一括削除する。
    void RemoveUserPlacementsIf(const std::function<bool(const SceneEntity&)>& matches);

    std::vector<SceneEntity> entities_;
    // guid→entities_のインデックス。FindEntity/UpdatePlacementをO(1)化するためのもの
    // (大規模レイアウト規模で毎フレームのエンティティ検索がO(N)線形走査だとボトルネックに
    // なるため)。
    std::unordered_map<Guid, size_t, GuidHash> guidToIndex_;
    uint64_t revision_ = 0;
    uint64_t structureRevision_ = 0;
    uint64_t persistentRevision_ = 0;
    // SetRuntimeSwitchOptionで一時的な値を書いた個体の、保存値の控え。
    std::unordered_map<Guid, std::vector<int32_t>, GuidHash> savedSwitchOptions_;
    // switchOptions を書き換え、変わったら true(pending と revision_ まで進める)。
    bool WriteSwitchOption(SceneEntity& entity, size_t index, int32_t value);
    std::vector<SwitchChange> pendingSwitchChanges_;
    std::vector<PlacementChange> pendingPlacementChanges_;
};

} // namespace rs::world
