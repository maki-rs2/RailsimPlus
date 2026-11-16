//
// PluginReferenceWalk.h - レイアウトがプラグインを参照している全部の場所を、決まった順に訪ねる
//
// 「どの場所の参照を数えるか」の一覧はここの訪問(VisitPluginReferences)だけが持つ。
// 見つからないプラグインの集計(読み取り)も、差し替え(書き換え)も、同じ訪問を使う。
// 訪問者は場所ごとの作業コピー(PluginReferenceSlot)を受け取り、書き換えたらtrueを返す。
// 訪問側が、trueのときだけ元の場所へ書き戻す。
//
// 訪問の順序は、同じ状態なら毎回同じになる(Undoが「何番目の参照か」で場所を指すため)。
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

namespace rs::editor {
class Editor;
}

namespace rs::world {
class SceneRegistry;
struct ManualPlacementOverlay;
struct SceneEntity;
struct TrackEdge;
struct TrackGraph;
}  // namespace rs::world

namespace rs::app {

class PluginSelector;
struct FormationCar;
struct ImportedWireSpan;
struct LayoutSceneCache;
struct LayoutSession;

// プラグインを参照している場所の種類。
enum class PluginReferenceSite {
    SceneryEntity,     // 配置した情景(Scenery)
    CarEntity,         // 配置した車両
    TrackSection,      // edgeの区間に敷設したレール系プラグイン
    Signal,            // 信号
    PlacedIndividual,  // 架線柱・橋脚の個体
    WireLine,          // 架線
    FormationCar,      // 編成の車両
    Environment,       // Sceneの環境プラグイン
};

struct PluginReference {
    std::string folderUtf8;
    PluginReferenceSite site = PluginReferenceSite::SceneryEntity;
};

// フォルダ表記の比較キー。区切りを`/`に揃え、末尾の区切りを除き、ASCIIの大文字小文字を無視する。
// 集計の行のキーと、差し替えの一致判定は、必ずこのキーで比べる。
std::string PluginFolderKey(const std::string& folderUtf8);

// フォルダの最後の名前(`D:/Plugins/Env/Sky`なら`Sky`)。末尾の区切りは無視する。
std::string PluginFolderLeafName(const std::string& folderUtf8);

// 区間の境界を保ってフォルダを置換し、重複を除く。toがnullptrなら削除する。
void EditFolderInEdge(rs::world::TrackEdge& edge, const std::string& from, const std::string* to);

// 訪問先1つぶんの作業コピー。訪問者がfolderやswitchOptionsを書き換えてtrueを返すと、元の場所へ書き戻される。
struct PluginReferenceSlot {
    PluginReferenceSite site = PluginReferenceSite::SceneryEntity;
    // 属するSceneの添字。Scene単位でない参照(配置した車両・編成)は0。
    std::size_t sceneIndex = 0;
    // 参照しているフォルダ。環境プラグインは、解決できたときはそのフォルダ、できなかったときはルート候補から組んだフォルダ。
    std::string folder;
    // スイッチ確定値の作業コピー。スイッチを持たない場所はnullptr。読み取りだけの訪問でもnullptr。
    std::vector<int32_t>* switchOptions = nullptr;
    // 編成の車両の作業コピー(folderはcar->folderUtf8と同じ値で始まる)。FormationCarのときだけ。
    FormationCar* car = nullptr;
    // 参照元そのもの(読み取り専用。書き戻す前の状態を控えるために使う)。
    const rs::world::TrackEdge* edge = nullptr;     // TrackSection
    const LayoutSceneCache* scene = nullptr;        // Environment
    const rs::world::SceneEntity* entity = nullptr;  // SceneryEntity / CarEntity
    // 場所の並びの中での位置(個体はレジストリかキャッシュの並びの中、編成の車は編成内の添字)。個体と編成の車だけ。
    std::size_t listIndex = 0;
    // entityがSceneRegistryの個体か(falseなら、別Sceneのキャッシュのmanual配置)。
    bool entityInRegistry = false;
    std::size_t formationIndex = 0;  // 編成の車の、編成の添字
    // 訪問者がtrueを返すときにこれをtrueにすると、書き戻しの代わりに参照元を削除する。
    // TrackSectionは、レールを残してそのフォルダだけを区間から外す。Environmentは削除できず、無視される。
    // 架線柱・橋脚を消すときは、それにつながる架線も一緒に消える。
    bool remove = false;
};

// 書き換えたらtrueを返す。読み取りだけならfalseを返す。
using PluginReferenceVisitor = std::function<bool(PluginReferenceSlot&)>;

// 訪問の対象。ConstをtrueにするとConstPluginReferenceContext(読み取り専用)になる。
template <bool Const>
struct BasicPluginReferenceContext {
    template <class T>
    using Ref = std::conditional_t<Const, const T, T>;

    Ref<rs::editor::Editor>& editor;
    Ref<rs::world::SceneRegistry>& sceneRegistry;
    Ref<LayoutSession>& session;
    Ref<PluginSelector>& selector;
    // 架線のライブ配列。nullptrなら、アクティブSceneもsessionのキャッシュ済みの値を使う。
    Ref<std::vector<ImportedWireSpan>>* importedWireSpans = nullptr;
    // 環境プラグインの名前→フォルダに使うプラグインルート候補(優先度順)。nullptrか空なら、
    // 解決できなかった環境は名前のまま参照になる。
    const std::vector<std::string>* pluginRoots = nullptr;
};

using PluginReferenceContext = BasicPluginReferenceContext<false>;
using ConstPluginReferenceContext = BasicPluginReferenceContext<true>;

// indexが指すSceneの、いまのgraph・オーバーレイ・架線スパン(アクティブならライブ状態、ほかはキャッシュ)。
// 範囲外はnullptr。架線スパンは、contextにライブ配列があるときだけアクティブSceneのものとして返す。
rs::world::TrackGraph* SceneGraphFor(PluginReferenceContext& context, std::size_t sceneIndex);
rs::world::ManualPlacementOverlay* SceneOverlayFor(PluginReferenceContext& context, std::size_t sceneIndex);
std::vector<ImportedWireSpan>* SceneWireSpansFor(PluginReferenceContext& context, std::size_t sceneIndex);
// 訪問するSceneの数(sessionが空ならライブ状態の1つ)。
std::size_t SceneCountOf(const PluginReferenceContext& context);

// 保存処理(SaveRslLayout)と同じ範囲の全参照を、決まった順に訪ねる。
// ・全Scene: アクティブなSceneはライブ状態、ほかのSceneはsessionのキャッシュ。sessionが空ならライブ状態だけ。
// ・一時的な配置物は訪ねない。
// ・TrackSectionは、edgeごとの重複を除いたフォルダ1つにつき1回。
void VisitPluginReferences(PluginReferenceContext& context, const PluginReferenceVisitor& visitor);

// 読み取り専用の版。訪問者の戻り値は無視する。
void VisitPluginReferences(const ConstPluginReferenceContext& context, const PluginReferenceVisitor& visitor);

}  // namespace rs::app
