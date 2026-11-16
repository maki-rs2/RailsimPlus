//
// PreloadTextureSet.h - 常駐スロットへ事前ロードすべきテクスチャ集合の算出
//
// スロットのテクスチャは読込時に一括で先読みする。対象を「今activeなエントリ」
// だけに絞ると、配置後に値が変わるシステムスイッチ(接近判定・カメラ距離LOD)で
// 別エントリがactiveになった瞬間に未ロードとなり、毎フレーム側のフォールバック
// (同期GPUアップロード)へ落ちてフレームが引っかかる。かといってプラグインの
// 全フレーム画像を先読みすると千枚規模になり、スロットのテクスチャ枠・VRAM・
// 読込時間のいずれももたなくなる。
//
// そこで「実行時に変わりうるスイッチが取りうる値」だけを仮定値として総当りし、
// activeになりうるエントリのテクスチャの和集合を求める。ユーザースイッチは既定では
// 読込時の確定値のまま評価する。ただし呼び出し側が予算(userSwitchTextureBudget)を
// 渡したときは、ChangeTexture/テクスチャを選ぶSetAnimationの発動条件が参照する
// ユーザースイッチも仮定値の軸に加える(配置後にスイッチを変えた瞬間の同期ロードを
// 避けるため)。結果が予算を超えるときは軸を足さない集合へ戻す。
//
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "../../render/pipeline/MeshPipeline.h"

namespace rs::app {

struct PluginSession;

// 総当りする仮定値の組合せ数の上限。これを超える場合は直積をやめ、1次元ずつ
// 動かす(他の次元は読込時の確定値)縮退モードへ落とす。組合せ数がそのまま
// 発動判定(customizer全件のIsTriggerActive)の反復回数になるため、読込時間が
// 跳ねないように頭を抑える。
inline constexpr size_t kMaxPreloadHypotheses = 64;

struct PreloadTextureSet {
    // SetAnimationが参照する解決済みテクスチャ(baseのみ。実行時の適用経路も
    // normal/pbrを渡さないため、登録キーを揃える)。
    std::vector<std::string> animationTextures;
    // ChangeTextureの候補(base/normal/pbr)。実行時フォールバックと同じ3つ組で
    // 登録しないと、先読み済みでもhasNormal/hasOrmが食い違う登録になる。
    std::vector<std::array<std::string, 3>> materialTextures;
    // 実際に評価した仮定値の組合せ数(ログ・調査用)。
    size_t hypothesisCount = 0;
    // 縮退モード(直積を諦めて1次元ずつ動かした)か。
    bool degraded = false;
    // ユーザースイッチの軸を足した集合を採用したか(ログ・調査用)。
    bool userSwitchAxesUsed = false;
    // 足そうとしたユーザースイッチの軸の数(0=足す対象が無い、または予算0)。
    size_t userSwitchAxisCount = 0;
    // ユーザースイッチの軸を足した集合が予算を超えたため、足さない集合へ戻したか。
    bool userSwitchAxesOverBudget = false;
};

// sessionが参照する「実行時に変わりうるスイッチ」の取りうる値を総当りし、
// activeになりうるテクスチャの和集合を返す(重複除去済み)。該当スイッチを
// 1つも参照しないsessionでは確定値1通りのみを評価する。
//
// userSwitchTextureBudget: 先読みするテクスチャがプールで使うスロット数の合計の上限
// (3つ組ごとに、法線/ORMがあれば3スロット・無ければ1スロット。アニメ分はbaseのみで
// 1スロット)。0ならユーザースイッチの軸を足さない。
// 1以上なら、ユーザースイッチの軸を足した集合のスロット数が上限以内のときだけその集合
// (軸を足さない集合との和集合)を返し、超えるときは軸を足さない集合を返す。
PreloadTextureSet CollectPreloadTextures(const PluginSession& session,
                                         size_t userSwitchTextureBudget = 0);

// setを1つのMeshPipelineへ積むときの件数。通し番号は「アニメ分→ChangeTexture分」の順で、
// 空のbaseの3つ組も1件に数える(積むときに飛ばす)。呼び出し側が数フレームに分けて積むために使う。
size_t PreloadEntryCount(const PreloadTextureSet& set);

// setの通し番号[begin, end)の範囲を pipeline へ積む(アニメ分はbaseのみ、ChangeTexture分は
// base/normal/pbrの3つ組。空のbaseは飛ばす)。メッシュの UploadMesh の後に呼ぶこと
// (UploadMesh がテクスチャスロットを初期化するため)。
// warnOnFailure: 積めなかった(枠切れ/読込失敗)ときに件ごとの警告を出すか。
void PreloadTextureEntries(rs::render::MeshPipeline& pipeline, ID3D12Device* device,
                           DirectX::ResourceUploadBatch& upload, const PreloadTextureSet& set,
                           size_t begin, size_t end, bool warnOnFailure);

} // namespace rs::app
