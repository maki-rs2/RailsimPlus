//
// MaterialRuntime.h - ChangeTexture/ChangeMaterial の実行時反映
//
// 描画非依存。ModelPart の ChangeTexture / changeMaterials（パース済み・条件
// 付き含む全候補）を発動文脈ごとに保持し、毎フレーム SwitchResolver で active
// 判定→宣言順に「最後に active だったもの勝ち(last active wins)」で
// マテリアルへ上書きする。非active時はベース状態（Build時に渡された .x 素の
// マテリアル値）へ戻す。
//
// テクスチャの実体(GPU texture)は mesh_pipeline 側で事前ロード済みという前提
// で、ここでは「path文字列」だけを保持し、適用時に呼び出し側(main.cpp)が
// path→texSlotOffset 解決して MeshPipeline::UpdateMaterialTexture() を呼ぶ。
//
#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../../schema/PluginSchema.h"
#include "../../schema/SwitchExpr.h"
#include "../../../render/mesh/Mesh.h"

namespace rs::legacy_plugin {

// 1件の ChangeTexture 候補（テクスチャ側）
struct MaterialTexEntry {
    int materialId = 0;
    std::string base, normal, pbr;
    rs::legacy_pi::TriggerInfo trig;
};

// 1件の ChangeMaterial 候補（色/材質側）
struct MaterialColorEntry {
    std::vector<int> materialIds;
    bool  hasDiffuse = false;  float diffuse[4]  = {0.8f, 0.8f, 0.8f, 1.0f};
    bool  hasAmbient = false;  float ambient[3]  = {0.8f, 0.8f, 0.8f};
    bool  hasSpecular = false; float specular[3] = {0, 0, 0};
    bool  hasEmissive = false; float emissive[3] = {0, 0, 0};
    bool  hasPower = false;    float power = 0.0f;
    rs::legacy_pi::TriggerInfo trig;
};

// 1件の ChangeAlpha 候補（アルファ値のみ。ChangeMaterial の Diffuse.a より後で
// 適用され、両方が同一マテリアルへ同時 active な場合はこちらが最終的に勝つ）。
struct MaterialAlphaEntry {
    int materialId = 0;
    float alpha = 1.0f;
    bool isDayAlpha = false;
    bool isNightAlpha = false;
    rs::legacy_pi::TriggerInfo trig;
};

// 適用結果（1マテリアル分）。呼び出し側はこれを見て
// MeshPipeline::UpdateMaterialColor/UpdateMaterialTexture へ反映する。
struct MaterialApplyResult {
    int materialId = 0;
    // テクスチャ（base が空ならベースへ戻す＝mesh の元テクスチャ or 無し）
    bool hasTextureOverride = false; // ChangeTexture の何かが active だったか
    std::string base, normal, pbr;
    // 色（ChangeMaterial 由来の上書き。ベース値がすでに入っている）。
    // color.useEnvMap / useBumpMap / forceAlphaTest / castShadow / receiveShadow は、EnvMap /
    // BumpMap / AlphaZeroTest / Transparent / NoCastShadow / NoReceiveShadow の指定を今のスイッチ
    // 状態で評価した値(ベースの値ではなく、Always の指定も含めて毎回計算し直す)。
    // color.forceTransparent は描画順のバケツで実行時に変えないため、ここでは計算しない。
    rs::render::Material color;

    // ChangeTexture が組み立てた法線/ORM画像を使ってよいか。法線は EnvMap か BumpMap の指定が
    // 成り立つ材質だけ、ORM は EnvMap の指定が成り立つ材質だけで使う(RailSim II -k-build)。
    bool AllowsNormal() const { return color.useEnvMap || color.useBumpMap; }
    bool AllowsOrm() const { return color.useEnvMap; }
};

// 1パーツ分の ChangeTexture/ChangeMaterial ランタイム。
class PartMaterialRuntime {
public:
    // ChangeTexture(textureSteps 中の宣言順) と part.changeMaterials とベースマテリアル(mesh の
    // ApplyPbr 前の値)を保持する。resolvedTexPaths は ChangeTexture の宣言順と
    // 同じ並び順で解決済み絶対パス(base/normal/pbr、無ければ空文字)を渡す
    // （LoadedModel::resolvedChangeTextures。plugin_loader 側で解決済み）。
    // 空 vector を渡した場合は未解決の相対パスをそのまま使う（テスト用）。
    void Build(const rs::legacy_pi::ModelPart& part,
              const std::vector<rs::render::Material>& baseMaterials,
              const std::vector<std::array<std::string, 3>>& resolvedTexPaths = {});

    // Build済みの同じ part について、ベースマテリアルと ChangeTexture の解決済みパスだけを
    // 差し替える(ChangeModel でジオメトリが変わったとき用)。発動条件(trig)を含む各エントリは
    // part から作り直さない。条件の多いプラグインでは Build のたびに全条件をコピーし直すのが
    // 重いため(ApplySwitch の多い情景で1回数ms)。結果は同じ引数で Build した場合と同じ。
    // 直前の Build と part の ChangeTexture 数が違うときは Build し直す。
    void Rebase(const rs::legacy_pi::ModelPart& part,
                const std::vector<rs::render::Material>& baseMaterials,
                const std::vector<std::array<std::string, 3>>& resolvedTexPaths);

    // 実行時に変わる材質の指定が無いか。発動条件のない EnvMap / BumpMap / 影・抜きの指定は
    // 読込時の値で変わらないので数えない(条件つきのものだけ数える)。
    bool Empty() const {
        return texEntries_.empty() && colorEntries_.empty() && alphaEntries_.empty() &&
               !hasConditionalFlags_;
    }

    bool HasDynamicMaterialChanges() const { return !Empty(); }

    // resolver で各エントリの active を判定し、宣言順「最後の active 勝ち」で
    // 全マテリアルの適用結果を返す（非 active なマテリアルはベース値のまま）。
    // active 判定は rs::legacy_pi::IsTriggerActive（trig.chain の全ステップを
    // AND評価。ネストした If/ApplySwitch にも対応）を使う。
    // generation は PluginSession::switchGeneration(スイッチ値が変化した
    // フレームだけ進む世代カウンタ)。前回呼び出しと同じ generation なら
    // customizer 全件の再評価をスキップしキャッシュ済み結果をそのまま返す
    // (スイッチが変わっていないのに毎フレーム全 ChangeTexture/ChangeMaterial/
    // ChangeAlpha を評価し直すコストを無くす。ChangeTextureが数百件あるプラグインでは
    // 毎フレームの評価が重さの一因になる)。
    std::vector<MaterialApplyResult> Apply(const rs::legacy_pi::SwitchResolver& resolve,
                                           uint64_t generation, float dayAlpha = 1.0f) const;

    // resolve の下で active な ChangeTexture エントリの解決済みパス
    // (base/normal/pbr の3つ組)を out へ追加する(重複除去は呼び出し側の責務)。
    // 事前ロード用。Apply()と違い勝者選定を行わず、activeな候補を全て挙げる
    // (実行時にどれが勝つかはスイッチ値次第なので、先読みは候補全部が要る)。
    // キャッシュ(cachedGeneration_)は使わない: 呼び出し側が仮定のスイッチ値を
    // 差し込んだresolveで複数回評価するため。
    void CollectTextureCandidatesForResolver(
        const rs::legacy_pi::SwitchResolver& resolve,
        std::vector<std::array<std::string, 3>>& out) const;

    // candidates のうち、ChangeTexture の発動条件(trig.chain)が参照しているスイッチ名を
    // referenced へ追加する。ChangeModel専用・色専用のスイッチは含めない
    // (テクスチャ先読みの仮定値の軸を増やしすぎないため)。ChangeTextureの
    // 対象外のエントリ(baseが空)は見ない。
    void CollectTextureTriggerSwitches(const std::vector<std::string>& candidates,
                                       std::set<std::string>& referenced) const;

private:
    // texEntries_ の base/normal/pbr を入れる(Build と Rebase で共通)。resolvedTexPaths の
    // 件数が ChangeTexture の数と合わないときは未解決の相対パスをそのまま使う(テスト用)。
    void AssignTexPaths(const rs::legacy_pi::ModelPart& part,
                        const std::vector<std::array<std::string, 3>>& resolvedTexPaths);

    std::vector<MaterialTexEntry>   texEntries_;
    std::vector<MaterialColorEntry> colorEntries_;
    std::vector<MaterialAlphaEntry> alphaEntries_;
    // EnvMap / BumpMap / AlphaZeroTest / Transparent / NoCastShadow / NoReceiveShadow の指定
    // (Always も含む。Apply は全件から材質ごとのフラグを計算し直す)。
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> envEntries_;
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> bumpEntries_;
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> alphaZeroEntries_;
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> transparentEntries_;
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> noCastShadowEntries_;
    std::vector<rs::legacy_pi::MaterialFlagCustomizer> noReceiveShadowEntries_;
    bool partCastShadow_ = true; // パーツの CastShadow(no なら全材質が影を落とさない)
    bool hasConditionalFlags_ = false; // 上の指定に発動条件つきが1件でもあるか
    std::vector<rs::render::Material> baseMaterials_;

    mutable std::vector<MaterialApplyResult> cachedResults_;
    mutable uint64_t cachedGeneration_ = ~uint64_t(0);
};

} // namespace rs::legacy_plugin
