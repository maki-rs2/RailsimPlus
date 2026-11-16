//
// PluginSelector.h - 種別ごとのプラグイン選択状態を保持する(敷設/編成/
// 情景配置の土台)。
//
// ・レール系(Rail/Tie/Girder/Pier/Line/Pole)は重複無しの読込キャッシュ
//   (trackPluginEntries_)へ保持する。ユーザーが実際に「今から敷設する/選択中の
//   edgeへ配置する」プラグインとして選ぶリストはこのキャッシュとは別に
//   Editor::layingSettings.defaultTrackPlugins / TrackEdge::trackPlugins(いずれも
//   folderUtf8の文字列リスト)側が持つ(役割を分けている)。
//   キャッシュからの削除UIは無い(他のedge/テンプレートが参照中でも問題なく
//   保つため、読込済みプラグインは明示的な破棄操作を持たない)。
// ・Trainは単体配置させず、必ず編成(Formation)へ追加する。複数編成を
//   作り分ける定義と選択はFormationCatalogが所有する。AddPathは選択中の編成へ
//   追加する(重複可、車両ごとに反転フラグを持つ)。
// ・それ以外(Station/Struct/Surface/Env/Skin等)は種別ごとに1件のみ保持する
//   (Surfaceの複数リスト化・実際の配置/レンダリングは行わない)。
//
// 実プラグイン(LoadedPlugin)は常駐させず、パス+種別+表示名の記述子のみを
// 保持する(GPU常駐管理は既存のPluginSession単一プラグイン読込パターンを
// そのまま踏襲し、本クラスでは扱わない)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "FormationCatalog.h"

#include "../../plugins/schema/PluginSchema.h"
#include "../../world/alignment/Guid.h"

namespace rs::world {
struct TrackEdge;
}

namespace rs::app {

// レール系プラグインの1エントリ。defはAddPath時にヘッダ+RailInfo/Profile等を
// 読込済みのPluginDefinitionをそのまま保持する(「追加された瞬間に読み込み」を
// 満たす。追加のディスクI/Oは発生しない)。離散配置(Pier)の間隔はUIで手動調整
// せず、def.pier.interval(PI定義値)をそのまま使う(座標/間隔の
// 手動調整はなく、PI定義を正とする)。
struct TrackPluginEntry {
    std::string folderUtf8;
    rs::legacy_pi::PluginType type = rs::legacy_pi::PluginType::Unknown;
    std::string displayName;
    rs::legacy_pi::PluginDefinition def;
};

class PluginSelector {
public:
    // folderUtf8 のプラグイン定義をヘッダのみパースし(loadMeshes=false)、
    // 種別に応じて適切な保持先(レール系リスト/編成リスト/単一選択スロット)へ
    // 追加する。失敗時(定義パース失敗/レール系の重複パス)はfalseを返し
    // errorOutへ理由を入れる。
    bool AddPath(const std::string& folderUtf8, std::string& errorOut);

    // レール系プラグインをキャッシュへ読み込み、登録済みの表記を返す(失敗時はnullopt、
    // AddPathと異なり重複をエラーにしない)。敷設テンプレート(Editor::
    // defaultTrackPlugins)やedge単位の配置リスト(TrackEdge::trackPlugins)へ
    // folderUtf8を追加する前に、まずこれで読込を確保する用途(
    // 「リストはキャッシュの閲覧UIではなく敷設対象の編集UI」という役割分離のため
    // AddPathとは別メソッドにしている。キャッシュからの削除は行わない=他のedgeが
    // 参照中でも安全)。
    std::optional<std::string> EnsureTrackPluginLoaded(const std::string& folderUtf8, std::string& errorOut);

    // 登録済みならその表記、未登録なら渡された表記を返す。
    std::string RegisteredTrackPluginFolder(const std::string& folderUtf8) const;

    // 区間と取り込み用の橋脚・架線柱参照を登録済みの表記へ揃え、区間内の重複を除く。
    void CanonicalizeTrackPluginFolders(rs::world::TrackEdge& edge) const;

    // folderUtf8に一致する読込済みTrackPluginEntryを探す(表示名/種別/間隔などの
    // 参照用、無ければnullptr)。
    const TrackPluginEntry* FindTrackPluginEntry(const std::string& folderUtf8) const;

    size_t AddFormation(const std::string& name, rs::world::Guid guid = {}) { return formationCatalog_.AddFormation(name, guid); }
    std::optional<size_t> DuplicateFormation(size_t index) { return formationCatalog_.DuplicateFormation(index); }
    void RemoveFormation(size_t index) { formationCatalog_.RemoveFormation(index); }
    void SetFormationName(size_t index, const std::string& name) { formationCatalog_.SetFormationName(index, name); }
    void SelectFormation(size_t index) { formationCatalog_.SelectFormation(index); }
    void RemoveFormationCar(size_t defIndex, size_t carIndex) { formationCatalog_.RemoveFormationCar(defIndex, carIndex); }
    void RestoreFormationCar(size_t defIndex, size_t position, FormationCar car) { formationCatalog_.RestoreFormationCar(defIndex, position, std::move(car)); }
    void ReplaceFormationCar(size_t defIndex, size_t carIndex, FormationCar car) { formationCatalog_.ReplaceFormationCar(defIndex, carIndex, std::move(car)); }
    void MoveFormationCarTo(size_t defIndex, size_t from, size_t to) { formationCatalog_.MoveFormationCarTo(defIndex, from, to); }
    void SetFormationCarReversed(size_t defIndex, size_t carIndex, bool reversed) { formationCatalog_.SetFormationCarReversed(defIndex, carIndex, reversed); }
    void SetFormationCarSwitchOptions(size_t defIndex, size_t carIndex, std::vector<int32_t> switchOptions) { formationCatalog_.SetFormationCarSwitchOptions(defIndex, carIndex, std::move(switchOptions)); }
    void MoveFormationCar(size_t defIndex, size_t carIndex, int delta) { formationCatalog_.MoveFormationCar(defIndex, carIndex, delta); }
    void AddUnresolvedFormationCar(size_t defIndex, const std::string& folderUtf8);
    bool InsertFormationCar(size_t defIndex, size_t position, const std::string& folderUtf8, std::string& errorOut);
    bool LoadFormationCar(const std::string& folderUtf8, FormationCar& carOut, std::string& errorOut) const;
    void RemoveSingleSlot(rs::legacy_pi::PluginType type);

    const std::vector<TrackPluginEntry>& TrackPluginEntries() const { return trackPluginEntries_; }
    const std::vector<Formation>& Formations() const { return formationCatalog_.Formations(); }
    size_t SelectedFormationIndex() const { return formationCatalog_.SelectedFormationIndex(); }
    const std::vector<FormationCar>& SelectedFormationCars() const { return formationCatalog_.SelectedFormationCars(); }
    const std::map<rs::legacy_pi::PluginType, std::string>& SingleSlots() const { return singleSlots_; }

    // trackPluginEntries_の内容(キャッシュへの追加)が変わるたびに増える。敷設
    // エディタ側はg_editor.revisionとのORで再構築トリガーに使う。
    uint64_t EntryRevision() const { return entryRevision_; }

private:
    // trackPluginEntries_ へ1件追加し、検索索引(normalizedIndex_)へ登録して
    // rawLookupCache_ を捨てる(否定キャッシュが古くなるため)。追加経路は
    // 必ずここを通し、索引と実体がずれないようにする。
    void AppendTrackPluginEntry(TrackPluginEntry&& entry);

    std::vector<TrackPluginEntry> trackPluginEntries_;

    // パス検索の索引。FindTrackPluginEntry は配置1件ごとに呼ばれ、正規化
    // (weakly_canonical)はファイルシステムを叩くため、線形走査×毎回正規化だと
    // 大規模レイアウトの再構築で数万回のFS問い合わせになり100ms超の固まりに
    // なる。正規化済みパス→添字の索引と、問い合わせ文字列
    // そのままをキーにしたメモを持ち、同じ綴りの再問い合わせを正規化なしで返す。
    // メモはあくまでSamePluginPath相当の判定結果のキャッシュであり、綴り違いの
    // 同一パスはミス時のフォールバック(正規化して索引を引く)で正しく解決される。
    static constexpr size_t kNotFound = static_cast<size_t>(-1);
    std::unordered_map<std::string, size_t>         normalizedIndex_;
    mutable std::unordered_map<std::string, size_t> rawLookupCache_;
    FormationCatalog formationCatalog_;
    std::map<rs::legacy_pi::PluginType, std::string> singleSlots_; // 種別 -> フォルダパス(1件のみ)
    uint64_t entryRevision_ = 0;
};

// フォルダパス2つが同一プラグインを指すか(正規化+大文字小文字無視で比較)。
bool SamePluginPath(const std::string& aUtf8, const std::string& bUtf8);

} // namespace rs::app
