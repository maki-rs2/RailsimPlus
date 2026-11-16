//
// SchemaWalk.cpp - PI パート/カスタマイザ走査（PluginSchema.cpp から分離）
//
// Axle/Body/Object3D 等のパーツブロックを再帰走査し、モデル参照・テクスチャ差し替え・
// 可動部（StaticMove/Rotation/DynamicRotation）・マテリアル変更・アニメーションを
// 収集する。ApplySwitch の Case 追跡や If/Else の条件文脈（WalkContext）を子へ
// 引き回す。詳細は SchemaParts.cpp を参照。
//
// 発動文脈は「祖先から自分までの TriggerStep のチェーン」として保持する
// （RailSim II の CSwitchApplier/CCustomizerSwitchEntry の再帰木を、既存の
// フラットリストアーキテクチャ向けにAND チェーンとしてフラット化した表現。
// 詳細は PluginSchema.h の TriggerInfo/TriggerStep コメント参照）。
// walkPart のブロックループと walkApplySwitch の Case 本体ブロックループは
// 共通の dispatchChildBlock() を通して同じ規則で子を処理する（片方だけが
// If/ApplySwitch を認識し、もう片方は認識せず条件を透明化してしまう非対称性を
// 起こさないため）。
//
#include "SchemaInternal.h"

#include "CustomizerParsers.h"

namespace rs::legacy_pi {

namespace {

// walkPart/walkApplySwitch の再帰に引き回す「現在の発動文脈」。
// chain = 祖先から自分までの TriggerStep 列（空 = Always）。子へ渡す際は
// コピーして末尾に1段 push_back する（上書きしない。上書きすると祖先の
// 条件が消える＝今回の修正対象そのもの）。
struct WalkContext {
    std::vector<TriggerStep> chain;
};

// WalkContext から TriggerInfo を作る。
TriggerInfo TriggerFromCtx(const WalkContext& ctx) {
    TriggerInfo t;
    t.chain = ctx.chain;
    return t;
}

void WalkPart(const PiBlock& b, ModelPart& part, const WalkContext& ctx);
void WalkApplySwitch(const PiBlock& sw, ModelPart& part, const WalkContext& outerCtx);

// 代入形式のカスタマイザ1件を発動文脈 ctx の下で収集する。
// walkPart と walkApplySwitch の両方から同じ規則で呼ばれる（片方だけが
// 特定のキーを認識する非対称性を避けるため、必ずこの関数へ集約する）。
void CollectAssignment(const PiAssignment& a, ModelPart& part, const WalkContext& ctx) {
    if (a.key == "ChangeModel") {
        // 候補として宣言順に積むだけで、確定は ResolveChangeModel が行う
        // （RailSim II は宣言順で最初にactiveな候補を採用する。詳細は同関数のコメント）。
        // 空文字列 (ChangeModel = "", 1.0;) は「この条件でパーツを消す」RailSim II の
        // 記法なので、破棄せず候補登録する（modelFile="" のまま
        // → LoadModelVariant が ok=false → ApplyModelVariant で非表示）。
        if (!a.values.empty() && a.values[0].kind == PiValue::Kind::String) {
            ChangeModelRef cm;
            cm.modelFile = a.values[0].AsString();
            if (a.values.size() > 1 && a.values[1].kind == PiValue::Kind::Number) {
                cm.modelScale = static_cast<float>(a.values[1].AsFloat(1.0));
                cm.hasScale = true;
            }
            cm.trig = TriggerFromCtx(ctx);
            part.changeModels.push_back(std::move(cm));
        }
    } else if (a.key == "ChangeTexture") {
        ChangeTextureRef ct;
        if (ParseChangeTexture(a, ct)) {
            ct.trig = TriggerFromCtx(ctx);
            part.AddChangeTexture(std::move(ct));
        }
    } else if (a.key == "EnvMap" || a.key == "BumpMap" || a.key == "AlphaZeroTest" ||
               a.key == "Transparent" || a.key == "NoCastShadow" ||
               a.key == "NoReceiveShadow" || a.key == "NoShadow") {
        MaterialFlagCustomizer flag;
        CollectIds(a, flag.materialIds);
        flag.trig = TriggerFromCtx(ctx);
        if (a.key == "EnvMap") part.envMaps.push_back(std::move(flag));
        else if (a.key == "BumpMap") part.bumpMaps.push_back(std::move(flag));
        else if (a.key == "AlphaZeroTest") part.alphaZeroTests.push_back(std::move(flag));
        else if (a.key == "Transparent") part.transparents.push_back(std::move(flag));
        else if (a.key == "NoCastShadow") part.noCastShadows.push_back(std::move(flag));
        else if (a.key == "NoReceiveShadow") part.noReceiveShadows.push_back(std::move(flag));
        else { // NoShadow: 同じ条件で両方へ入れる
            part.noCastShadows.push_back(flag);
            part.noReceiveShadows.push_back(std::move(flag));
        }
    } else if (a.key == "SetAnimation") {
        if (a.values.size() >= 2 && a.values[0].kind == PiValue::Kind::Number) {
            SetAnimationCustomizer sa;
            sa.materialId = a.values[0].AsInt();
            sa.animationName = a.values[1].AsString();
            sa.trig = TriggerFromCtx(ctx);
            part.AddSetAnimation(std::move(sa));
        }
    } else if (a.key == "ShiftTexture" || a.key == "ScaleTexture" ||
               a.key == "RotateTexture" || a.key == "TransformTexture") {
        TextureTransformCustomizer tt;
        if (ParseTextureTransform(a.key, a, tt)) {
            tt.trig = TriggerFromCtx(ctx);
            part.AddTextureTransform(std::move(tt));
        }
    } else if (a.key == "ChangeAlpha") {
        ChangeAlphaCustomizer ca;
        if (ParseChangeAlpha(a, ca)) {
            ca.trig = TriggerFromCtx(ctx);
            part.changeAlphas.push_back(ca);
        }
    } else if (a.key == "AnalogClock") {
        // ブロックでなく単一代入(RailSim II もBeginBlock/EndBlockを使わない)。
        if (!a.values.empty() && a.values[0].kind == PiValue::Kind::Ident) {
            const std::string& hand = a.values[0].text;
            StaticCustomizer sc;
            sc.kind = StaticCustomizer::Kind::AnalogClock;
            if (hand == "Hour") sc.clockHandType = 0;
            else if (hand == "Minute") sc.clockHandType = 1;
            else if (hand == "Second") sc.clockHandType = 2;
            else return; // 未知の針種別は他の未知構文と同じく無視
            sc.trig = TriggerFromCtx(ctx);
            part.customizers.push_back(std::move(sc));
        }
    }
}

// 子ブロック c を ctx の下でディスパッチする（walkPart のブロックループと
// walkApplySwitch の Case 本体ブロックループの共通処理。片方だけが If/
// ApplySwitch を認識する非対称を作らないため、必ずこの関数
// 経由で統一する）。
//
// If/Else の対応関係はブロック列の呼び出し側（ループ）が ioWasIf/ioElseCtx を
// ローカル変数として持ち、このブロックを跨いで引き回す（同一階層の兄弟として
// 連続するIf〜Elseのみ対応させる。walkPart の一区間、walkApplySwitch の
// 1つのCase本体、それぞれ独立した区間として ioWasIf を新区間の先頭で false に
// リセットして呼び出すこと）。
void DispatchChildBlock(const PiBlock& c, ModelPart& part, const WalkContext& ctx,
                        bool& ioWasIf, WalkContext& ioElseCtx) {
    if (c.name == "JointZY" || c.name == "Joint3D") {
        ioWasIf = false;
        return;
    }
    if (c.name == "StaticMove" || c.name == "StaticRotation" ||
        c.name == "DynamicRotation") {
        part.customizers.push_back(ParseCustomizerBlock(c, TriggerFromCtx(ctx)));
        ioWasIf = false;
        return;
    }
    if (c.name == "TrackWind") {
        WindTrackerCustomizer tracker;
        if (ParseWindTracker(c, TriggerFromCtx(ctx), tracker)) part.windTrackers.push_back(std::move(tracker));
        ioWasIf = false;
        return;
    }
    if (c.name == "Windmill") {
        WindmillCustomizer windmill;
        if (ParseWindmill(c, TriggerFromCtx(ctx), windmill)) part.windmills.push_back(std::move(windmill));
        ioWasIf = false;
        return;
    }
    if (c.name == "TrackingMove") {
        TrackingMoveCustomizer move;
        if (ParseTrackingMove(c, TriggerFromCtx(ctx), move)) part.trackingMoves.push_back(std::move(move));
        ioWasIf = false;
        return;
    }
    if (c.name == "TrackingRotation") {
        TrackingRotationCustomizer rotation;
        if (ParseTrackingRotation(c, TriggerFromCtx(ctx), rotation)) part.trackingRotations.push_back(std::move(rotation));
        ioWasIf = false;
        return;
    }
    if (c.name == "ChangeMaterial") {
        part.changeMaterials.push_back(ParseChangeMaterial(c, TriggerFromCtx(ctx)));
        ioWasIf = false;
        return;
    }
    if (c.name == "ApplySwitch") {
        WalkApplySwitch(c, part, ctx);
        ioWasIf = false;
        return;
    }
    if (c.name == "If") {
        TriggerStep step;
        step.trigger = TriggerKind::Condition;
        step.condition = c.label;
        step.negateCondition = false;
        WalkContext innerCtx = ctx;
        innerCtx.chain.push_back(step);
        WalkPart(c, part, innerCtx);

        TriggerStep elseStep = step;
        elseStep.negateCondition = true;
        ioElseCtx = ctx;
        ioElseCtx.chain.push_back(elseStep);
        ioWasIf = true;
        return;
    }
    if (c.name == "Else") {
        // 直前に If が無い（同一区間内で連続していない）Else は対応関係不明
        // として黙ってスキップする。
        if (ioWasIf) WalkPart(c, part, ioElseCtx);
        ioWasIf = false;
        return;
    }
    WalkPart(c, part, ctx);
    ioWasIf = false;
}

// ApplySwitch ブロック配下は Case ラベルが子ブロックではなく members 上の
// フラットなマーカーとして並ぶ(構文: `Case n: <block/assignment...>` が
// 次の Case/Default まで同一階層で連なる)。members を出現順に見て現在の
// Case 文脈（チェーンの末尾1段）を更新しながら、子は dispatchChildBlock で
// 統一的に処理する。
void WalkApplySwitch(const PiBlock& sw, ModelPart& part, const WalkContext& outerCtx) {
    // Default 判定用: このApplySwitchが宣言する全 Case(非Default)の値の和集合を
    // 事前に1パス収集する。Default の active 判定はこれ単体で完結し、
    // 「マッチしたCaseの本体が空/別種customizerのみでleafが無い」場合でも
    // 誤判定しない（他エントリの横断スキャンに依存しない設計。詳細は
    // PluginSchema.h の TriggerStep::declaredCaseValues コメント参照）。
    std::vector<int> declared;
    for (const auto& m : sw.members)
        if (m.kind == PiMember::Kind::CaseLabel && !m.isDefault)
            for (int v : m.caseValues) declared.push_back(v);

    bool haveCase = false;
    WalkContext caseCtx = outerCtx; // 現在の Case 本体で使う chain（Case未到達ならouterCtxのまま）
    bool wasIf = false;
    WalkContext elseCtx;

    for (const auto& m : sw.members) {
        if (m.kind == PiMember::Kind::CaseLabel) {
            haveCase = true;
            TriggerStep step;
            step.trigger = TriggerKind::SwitchCase;
            step.switchName = sw.label;
            step.switchIsExpression = sw.labelIsExpression;
            step.isDefaultCase = m.isDefault;
            if (m.isDefault) step.declaredCaseValues = declared;
            else step.caseValues = m.caseValues;
            caseCtx = outerCtx;
            caseCtx.chain.push_back(step);
            wasIf = false; // 新しい Case 本体区間: If/Else の対応関係をリセット
            continue;
        }
        const WalkContext& useCtx = haveCase ? caseCtx : outerCtx;
        if (m.kind == PiMember::Kind::Block) {
            const PiBlock& c = sw.blocks[m.index];
            DispatchChildBlock(c, part, useCtx, wasIf, elseCtx);
            continue;
        }
        if (m.kind == PiMember::Kind::Assignment)
            CollectAssignment(sw.assignments[m.index], part, useCtx);
    }
}

// パーツ（Axle/Body/Object3D 等）とその配下（If/ApplySwitch/Case 本体を含む
// ネスト全体）を再帰的に走査し、モデル参照とテクスチャ差し替えを収集する。
// ctx は現在の発動文脈（祖先から自分までの TriggerStep チェーン）。
void WalkPart(const PiBlock& b, ModelPart& part, const WalkContext& ctx) {
    // モデル参照
    if (const PiAssignment* mf = b.Find("ModelFileName")) {
        if (mf->First() && !mf->First()->AsString().empty())
            part.modelFile = mf->First()->AsString();
    }
    if (const PiAssignment* ms = b.Find("ModelScale")) {
        if (ms->First()) part.modelScale = static_cast<float>(ms->First()->AsFloat(1.0));
    }

    if (const PiAssignment* cs = b.Find("CastShadow")) {
        if (cs->First()) {
            part.castShadowSpecified = true;
            part.castShadow = cs->First()->IsYes();
        }
    }

    // 代入と子ブロックを「ソース上の出現順」で走査する（members を使う）。
    // 代入を先に全件、次にブロックを全件…と分けて回すと宣言順が壊れ、
    // ChangeModel の先勝ち判定（ResolveChangeModel）が誤った候補を選ぶ。
    // 子ブロックは If/ApplySwitch/Case/Else/ChangeMaterial 等。
    // JointZY/Joint3D はモデルを持たない接続情報なので中は掘らない。
    // dispatchChildBlock が If〜Else の対応関係（同一階層で連続する兄弟のみ）を
    // wasIf/elseCtx で追跡する。
    bool wasIf = false;
    WalkContext elseCtx;
    for (const PiMember& m : b.members) {
        if (m.kind == PiMember::Kind::Assignment) {
            CollectAssignment(b.assignments[m.index], part, ctx);
            continue;
        }
        if (m.kind == PiMember::Kind::Block)
            DispatchChildBlock(b.blocks[m.index], part, ctx, wasIf, elseCtx);
    }
}

// TriggerInfo(chain)が現在activeかどうかを判定する共通関数。全ステップをAND
// 評価する。ChangeModel/MaterialRuntime/AnimationRuntime/MoverRuntime の
// leaf active判定はすべてこれを共有する。
bool IsTriggerActiveImpl(const TriggerInfo& trig, const SwitchResolver& resolve) {
    for (const auto& step : trig.chain) {
        bool active = false;
        if (step.trigger == TriggerKind::Condition) {
            ExprResult r = EvalSwitchExpr(step.condition, resolve);
            bool v = r.ok && r.value != 0;
            active = step.negateCondition ? !v : v;
        } else { // SwitchCase
            // 式のラベルは評価した整数値で Case を選ぶ(RailSim II の CSwitchApplier::GetSwitchValue)。
            // パースできない式は0として扱う。
            int32_t v = 0;
            if (step.switchIsExpression) {
                const ExprResult r = EvalSwitchExpr(step.switchName, resolve);
                v = r.ok ? r.value : 0;
            } else if (resolve) {
                v = resolve(step.switchName);
            }
            if (step.isDefaultCase) {
                bool inDeclared = false;
                for (int dv : step.declaredCaseValues)
                    if (dv == v) { inDeclared = true; break; }
                active = !inDeclared;
            } else {
                for (int cv : step.caseValues)
                    if (cv == v) { active = true; break; }
            }
        }
        if (!active) return false;
    }
    return true;
}

// 収集済み ChangeModel 候補から有効な1件を modelFile/modelScale へ反映する。
// ロード時点はスイッチ初期値（全スイッチ 0）で評価する（実際の判定ロジックは
// ResolveChangeModel() に一本化し、PartDynamicModel の実行時解決と共有する。
// ロード時/実行時でロジックが乖離するとスイッチ変更で反映されるべきモデルが
// フレーム1で一瞬別のものへ「チラつく」ため、必ず同じ関数を使うこと）。
// baseModelFile/baseModelScale へ上書き前の素の値(ModelFileName/ModelScale)を
// 取っておく（実行時解決が「どの候補もactiveでない」場合のベースに使う）。
void ResolveChangeModel(ModelPart& part) {
    part.baseModelFile = part.modelFile;
    part.baseModelScale = part.modelScale;
    SwitchResolver zeroResolver = [](const std::string&) { return int32_t(0); };
    ChangeModelResolveResult r = ResolveChangeModel(part, zeroResolver);
    if (r.variantIndex >= 0 && static_cast<size_t>(r.variantIndex) < part.changeModels.size())
        part.modelFile = part.changeModels[static_cast<size_t>(r.variantIndex)].modelFile;
    part.modelScale = r.modelScale;
}

} // namespace

// 既存呼び出し（トップレベル/PrimaryAssembly からの走査開始）用の薄いラッパ。
// 発動文脈は Always（空chain）から開始する。
void WalkPart(const PiBlock& b, ModelPart& part) {
    WalkContext ctx;
    WalkPart(b, part, ctx);
    ResolveChangeModel(part);
}

bool IsTriggerActive(const TriggerInfo& trig, const SwitchResolver& resolve) {
    return IsTriggerActiveImpl(trig, resolve);
}

ChangeModelResolveResult ResolveChangeModel(const ModelPart& part,
                                            const SwitchResolver& resolve) {
    ChangeModelResolveResult out;
    out.modelScale = part.baseModelScale;
    for (size_t i = 0; i < part.changeModels.size(); ++i) {
        const ChangeModelRef& cm = part.changeModels[i];
        if (!IsTriggerActive(cm.trig, resolve)) continue;
        out.variantIndex = static_cast<int>(i);
        if (cm.hasScale) out.modelScale = cm.modelScale;
        break; // 先勝ち（下記コメント参照）
    }
    return out;
}

} // namespace rs::legacy_pi
