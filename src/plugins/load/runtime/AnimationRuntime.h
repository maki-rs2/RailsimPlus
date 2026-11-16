//
// AnimationRuntime.h - マテリアル単位のUV変換を生み出す customizer 群の実行時反映
//
// 描画非依存。「1マテリアルにつき最終的なUV変換は1つ」というGPU側の制約
// (MeshPipeline::UpdateMaterialUvTransform)に対し、その値を生み出しうる宣言を
// ModelPart::textureSteps の宣言順に1回たどり、材質ごとの勝者を決める:
//   1. SetAnimation: PluginDefinition::animations(LoadedPlugin::resolvedAnimations で
//      解決済みのテクスチャパス込み)から、発動文脈(TriggerInfo)ごとに今のframe文を
//      求める。UV系(SlideUV/TiledUV/RotationUV)は2x3アフィンUV変換を、Simple(画像
//      差し替え、Frame = "path", duration;)はテクスチャを、変換付きのSimpleは両方を返す。
//   2. texture-transformer(ShiftTexture/ScaleTexture/RotateTexture/TransformTexture):
//      DefineAnimation を介さない即値の2x3アフィンUV変換(時間非依存)。
//   3. ChangeTexture: 実体(パス・法線・ORM)は PartMaterialRuntime が持つ。ここでは
//      「activeなら、それより前のSetAnimationのテクスチャを打ち消す」目印としてだけ
//      参加する。
// 同じ材質では、あとに宣言されたものが勝つ。UVとテクスチャは別々に勝者を持つ
// (変換のないSimple frameはUVに触れず、UV系frameの画像がテクスチャを決める)。
// Numbered(連番差し替え型、%d書式の連番展開)は扱わない(resolvedTexが
// 常に空文字なのでhasTexture=falseに倒れる)。
// 1つの DefineAnimation 内で frame 文が複数連なる場合（実データに実例あり:
// スクロール→静止保持の2segment等）は、宣言順の累積時間で現在の frame文を
// 特定してから、その中のサブフレーム(UV系のみ)を経過時間で選ぶ。
//
#pragma once

#include <set>
#include <string>
#include <vector>

#include "../../schema/PluginSchema.h"
#include "../../schema/SwitchExpr.h"

namespace rs::legacy_plugin {

// uv' = uv * [[a, c], [b, d]] + [tx, ty] （列ベクトル規約。mesh_shader.hのHLSL式と一致させる）
struct UvTransform {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};

// 1件のSetAnimation発動候補。DefineAnimationの中身(frames)とUV系テクスチャの
// 解決済みパスをBuild時にコピー保持する(PluginDefinition/LoadedPluginの寿命に
// 依存しないため。MaterialRuntimeと同じ方針)。
struct AnimationEntry {
    int materialId = 0;
    std::vector<rs::legacy_pi::AnimationFrame> frames;
    std::vector<std::string> resolvedTex; // frames と同じ並び。UV系/Simple系で埋まる(Numberedは空)
    rs::legacy_pi::TriggerInfo trig;
    float totalSeconds = 0.0f; // 全frame文の合計時間(ループ周期、秒)
    // frames の中に解決済みテクスチャを持つ Simple(画像差し替え)frame が1つでも
    // あるか(Build時に1回だけ判定)。Update()でこのエントリのmaterialIdを
    // 「テクスチャ状態を管理しうる materialId」としてマークするために使う
    // (AnimationApplyResult::textureCapable参照)。
    bool hasResolvedTexFrame = false;
};

// texture-transformer(ShiftTexture/ScaleTexture/RotateTexture/TransformTexture)の
// 1件。時間経過に依存せず、active な間は常に同じ UvTransform を返す。
struct StaticUvEntry {
    int materialId = 0;
    UvTransform uv;
    rs::legacy_pi::TriggerInfo trig;
};

// materialId ごとの適用結果。
struct AnimationApplyResult {
    int materialId = 0;
    bool hasTexture = false;  // true なら、テクスチャの勝者が active 中の UV系/Simple系 frame のとき
    std::string texturePath;  // hasTexture 時のみ有効(絶対解決済み)
    UvTransform uv;            // 常に有効(UV系が無関与の場合は恒等)
    // UVを上書きした勝者がアニメか(「今まさに経過時間が指しているframe文」がUV系、
    // または変換付きのSimple frameのとき。エントリ全体の性質ではなくframe文ごとに
    // 決まるので、スクロール→静止保持のような混在アニメでも静止保持中は勝者にならない)。
    // texture-transformerが勝者のときはfalse。
    bool isUvNow = false;
    // このmaterialIdが(現フレームでactiveか否かに関わらず)Simple系SetAnimationの
    // 対象でありうるか。true なのに hasTexture==false の場合は「Simpleアニメが
    // 非active化してテクスチャを離した」ことを表し、呼び出し側(main.cpp)は
    // MeshPipeline::RevertMaterialTexture でベース(または ChangeTexture 由来)の
    // テクスチャへ明示的に戻す必要がある(MaterialRuntime配下に無いmaterialId=
    // ChangeTextureを1つも持たないパーツでは他に誰も戻さないため、戻さないと
    // 直前の画像に貼り付いたままになる)。false のmaterialId(UV系のみ関与)は
    // 一切テクスチャに触れない。
    bool textureCapable = false;
};

// ChangeTexture を宣言順の1歩として扱うための印。
// ChangeTexture の実体は PartMaterialRuntime が持つので、材質と発動文脈だけ持つ。
struct TextureMarkerEntry {
    int materialId = 0;
    rs::legacy_pi::TriggerInfo trig;
};

// 1パーツ分の「テクスチャとUV変換を生み出すcustomizer群(SetAnimation+
// texture-transformer+ChangeTextureの目印)」ランタイム。
class PartAnimationRuntime {
public:
    // part.textureSteps を宣言順にたどり、SetAnimation は animationName で
    // defs(=PluginDefinition::animations)を名前引きし、resolvedAnimTex
    // (=LoadedPlugin::resolvedAnimations から .base を取り出したもの。defs と同じ
    // 並び・件数)からUV系テクスチャの絶対パスを取り込む。texture-transformer
    // (ShiftTexture等)とChangeTextureの目印も、宣言順を保ったまま取り込む。
    // frameRateFps は durationFrames を実時間へ換算する基準フレームレート。
    // 定義ファイルの版で異なるため、呼び出し側は必ず
    // rs::legacy_plugin::AnimationFrameRateFps(plugin) の値を渡すこと
    // (取り違えると全アニメが2倍速/半速で再生される)。
    void Build(const rs::legacy_pi::ModelPart& part,
              const std::vector<rs::legacy_pi::AnimationDef>& defs,
              const std::vector<std::vector<std::string>>& resolvedAnimTex,
              float frameRateFps);

    bool Empty() const { return entries_.empty() && staticEntries_.empty(); }

    // 時間駆動のSetAnimationエントリを持つか(texture-transformerによる静的UV変換
    // のみのパーツはfalse)。レイアウト配置側の毎フレーム更新対象の絞り込みに使う
    // (Scenery配置のtexture-transformerはPluginSession::ApplySwitchOptionsAndBakeが
    // 読込時に1回だけ焼き込めば十分。レイアウトのuserSwitchesは配置後変化しないため)。
    bool HasTimedEntries() const { return !entries_.empty(); }

    // entries_/staticEntries_のいずれかを1件以上持つか。配置済みTrain車両向けの
    // 毎フレーム再適用の絞り込みに使う(HasTimedEntriesと異なりtexture-transformer
    // 単体のパーツもtrueになる。Train車両は_FRONT等のシステムスイッチが
    // ApplySwitchOptionsAndBakeの読込時焼き込み後に変化しうるため、
    // texture-transformerのみのパーツでも毎フレーム再評価が必要
    // (If !"_FRONT"{ShiftTexture}Else{ShiftTexture}のElse側(個体反転車両=_FRONT=1)が
    // 永久に反映されなくなるため)。
    bool HasEntries() const { return !entries_.empty() || !staticEntries_.empty(); }

    // 全エントリの発動判定(MaterialRuntimeと同じDefault/If-AND評価)を行い、
    // active なエントリの再生位相を clockSeconds(呼び出し側が持つ共有アニメー
    // ション時計、実時間の絶対秒)から fmod(clockSeconds, 周期) で求める。
    // エントリ個別の経過時間は持たない=同一周期のアニメは全パーツ・全エントリで
    // 常に同位相になり、切り替えタイミングが揃う(発車標の行先/種別/両数など)。
    // またdt累積のfloat誤差が溜まらないため、フレームレートにも依存しない。
    // 副作用として「active化した瞬間に先頭フレームから再生」ではなく共有時計の
    // 現在位相から表示される(RailSim IIのグローバルタイマ挙動に合わせた仕様)。
    // 宣言順に1回たどって材質ごとの勝者を決める(アルゴリズムはUpdateの実装コメント)。
    // 結果は Results() で取得する。
    // generation は PluginSession::switchGeneration。前回呼び出しと同じ
    // generationの間は「どのエントリがactiveか」の判定(IsTriggerActive呼び出し)
    // 自体をスキップし、直前に判定した active/非active をそのまま使い回す
    // (時間経過に伴うUVフレーム送り=EvalEntryは毎フレーム必要なため、こちらは
    // 毎フレーム行う。SetAnimation・TextureTransformが数百件ある
    // プラグインでは、発動判定が重さの一因になる)。
    void Update(double clockSeconds, const rs::legacy_pi::SwitchResolver& resolve,
               uint64_t generation);

    // materialId ごとの適用結果(宣言順で最初に登場した materialId の順)。
    // 同じ材質では、あとに宣言されたものが勝つ。UVとテクスチャは別々に勝者を持つ:
    //   - UV勝者: texture-transformer、UV系SetAnimation(今のframe文がUV系のとき)、
    //     変換付きのSimple frame。
    //   - テクスチャ勝者: 解決済みテクスチャを持つ今のframe文のSetAnimation。
    //     それより後のactiveなChangeTextureが打ち消す(hasTexture=falseで返し、
    //     実際のテクスチャはPartMaterialRuntimeの結果が生きる)。
    const std::vector<AnimationApplyResult>& Results() const { return results_; }

    // 現在activeな時間駆動エントリが再生中に参照しうる、解決済みテクスチャパスを
    // outへ追加する(重複除去は呼び出し側の責務)。事前ロード対象の絞り込み用:
    // 1プラグインのDefineAnimationが持つ全フレーム画像は数百〜千枚規模になりうる
    // 一方、スイッチ確定後にactiveになるエントリはごく一部なので、全件を先読みすると
    // テクスチャ枠・VRAM・読込時間のいずれももたなくなる。
    // 発動判定のキャッシュはUpdate()と共有する(同じgenerationなら再判定しない)。
    void CollectActiveTimedTextures(const rs::legacy_pi::SwitchResolver& resolve,
                                    uint64_t generation, std::vector<std::string>& out);

    // CollectActiveTimedTexturesの「発動判定キャッシュを使わない」版。呼び出し側が
    // 実際のスイッチ値とは異なる仮定値(システムスイッチの取りうる値)を差し込んだ
    // resolveで複数回評価し、その和集合を事前ロード対象にするために使う。
    // generationキャッシュを共有すると仮定値での判定結果が実フレーム側へ漏れる
    // ため、こちらは毎回IsTriggerActiveを評価する(読込時のみの経路)。
    void CollectTimedTexturesForResolver(const rs::legacy_pi::SwitchResolver& resolve,
                                         std::vector<std::string>& out) const;

    // candidates のうち、テクスチャを選ぶSetAnimation(resolvedTexが非空のエントリ)の
    // 発動条件が参照しているスイッチ名を referenced へ追加する。UV変換だけのエントリと
    // texture-transformerは見ない(先読み対象のテクスチャを変えないため)。
    void CollectTextureTriggerSwitches(const std::vector<std::string>& candidates,
                                       std::set<std::string>& referenced) const;

    // texture-transformer(時間非依存)の active な UV変換だけを materialId
    // ごとに列挙する(同一materialIdは宣言順で最後にactiveなものが勝ち)。
    // 読込時の焼き込み用。
    // 時間駆動エントリ(entries_、SetAnimation)は一切評価しない
    // (1フレーム目を焼き込んでしまうため)。
    struct StaticUvResult {
        int materialId = 0;
        UvTransform uv;
    };
    void CollectStaticUv(const rs::legacy_pi::SwitchResolver& resolve, uint64_t generation,
                         std::vector<StaticUvResult>& out);

private:
    // 宣言順の1歩。index は kind に応じて staticEntries_ / entries_ / markers_ の添字。
    enum class StepKind { Transform, Animation, ChangeTexture };
    struct StepRef {
        StepKind kind;
        size_t   index;
    };

    // sa が指す DefineAnimation が見つかれば entries_ の末尾へ追加して true を返す。
    bool AddAnimationEntry(const rs::legacy_pi::SetAnimationCustomizer& sa,
                           const std::vector<rs::legacy_pi::AnimationDef>& defs,
                           const std::vector<std::vector<std::string>>& resolvedAnimTex,
                           bool haveResolved);

    // staticEntries_/entries_ の発動判定結果を generation 単位でキャッシュする
    // (Update()とCollectActiveTimedTextures()の共通処理)。
    void EnsureActiveCache(const rs::legacy_pi::SwitchResolver& resolve, uint64_t generation);

    // durationFrames→秒の換算に使う基準フレームレート(Build時に確定)。
    float frameRateFps_ = 30.0f;
    std::vector<AnimationEntry> entries_;
    std::vector<StaticUvEntry>  staticEntries_; // texture-transformer(時間非依存)
    std::vector<TextureMarkerEntry> markers_;   // ChangeTexture の目印
    std::vector<StepRef> steps_;                // 宣言順
    std::vector<AnimationApplyResult> results_;

    // generation単位でキャッシュする発動判定結果(staticEntries_/entries_と同じ並び)。
    std::vector<bool> cachedStaticActive_;
    std::vector<bool> cachedAnimActive_;
    std::vector<bool> cachedMarkerActive_;
    uint64_t          cachedActiveGeneration_ = ~uint64_t(0);
};

} // namespace rs::legacy_plugin
