//
// SchemaEffectors.cpp - PrimaryAssembly直下のエフェクタ(SoundEffect/Headlight/Particle、駅のレール接続の代入)収集。
//
#include "SchemaInternal.h"

#include <cmath>

#include "CustomizerParsers.h"

namespace rs::legacy_pi::schema_detail {
namespace {

TriggerInfo MakeTriggerInfo(const std::vector<TriggerStep>& chain) {
    TriggerInfo trig;
    trig.chain = chain;
    return trig;
}

// 端点番号から駅内のプラットフォーム番号と端点(偶数=始点側)を求める。負の番号は求めない。
void ResolvePlatformEnd(int railEnd, int& platformId, int& platformEnd) {
    if (railEnd < 0) return;
    platformId = railEnd / 2;
    platformEnd = railEnd % 2;
}

// ConnectRail / BranchRail / DisconnectRail の代入1件を読む。整数でない値や個数違いのときはfalse(読めない1件は飛ばす)。
bool ParseRailEffector(const PiAssignment& assignment, const TriggerInfo& trig, RailEffectorDef& out) {
    RailEffectorDef effector;
    size_t endCount = 2;
    if (assignment.key == "ConnectRail") effector.kind = RailEffectorDef::Kind::Connect;
    else if (assignment.key == "BranchRail") effector.kind = RailEffectorDef::Kind::Branch;
    else if (assignment.key == "DisconnectRail") {
        effector.kind = RailEffectorDef::Kind::Disconnect;
        endCount = 1;
    } else return false;
    if (assignment.values.size() != endCount) return false;
    for (const PiValue& value : assignment.values) {
        if (value.kind != PiValue::Kind::Number || value.number != std::floor(value.number)) return false;
    }
    effector.railEnd1 = assignment.values[0].AsInt();
    ResolvePlatformEnd(effector.railEnd1, effector.platformId1, effector.platformEnd1);
    if (endCount == 2) {
        effector.railEnd2 = assignment.values[1].AsInt();
        ResolvePlatformEnd(effector.railEnd2, effector.platformId2, effector.platformEnd2);
    }
    effector.trig = trig;
    out = std::move(effector);
    return true;
}

bool IsRailEffectorKey(const std::string& key) {
    return key == "ConnectRail" || key == "BranchRail" || key == "DisconnectRail";
}

// エフェクタ1件を種類に応じて読み、読めたものだけ out へ足す(読めない1件は飛ばす)。
void CollectEffector(const PiBlock& block, const TriggerInfo& trig, EffectorCollection& out) {
    if (block.name == "SoundEffect") {
        SoundEffectRef soundEffect;
        if (ParseSoundEffect(block, trig, soundEffect)) out.soundEffects.push_back(std::move(soundEffect));
    } else if (block.name == "Headlight") {
        // 収集は記述順なので、直前に収集した灯具のLensFlare(引き継ぎ済み)が「以前に定義されたもの」になる。
        const std::optional<LensFlareDef> previousLensFlare =
            out.headlights.empty() ? std::nullopt : out.headlights.back().lensFlare;
        HeadlightDef headlight;
        if (ParseHeadlight(block, trig, previousLensFlare, headlight)) out.headlights.push_back(std::move(headlight));
    } else if (block.name == "Particle") {
        ParticleDef particle;
        if (ParseParticle(block, trig, particle)) out.particles.push_back(std::move(particle));
    }
}

// container配下(If/Else/ApplySwitch/Caseのネストを含む)を再帰的に走査し、
// SoundEffect・Headlight・Particleを同じ走査で収集する。chainは祖先から自分までの発動条件チェーン。
//
// ApplySwitch配下はCaseラベルがブロックではなくmembers上のフラットな
// マーカーとして並ぶ(walkApplySwitch/SchemaWalk.cppと同じ構文形。実データでは
// 警報音等のSoundEffectがApplySwitchのCase配下に置かれる形が多いため、If/Elseだけ
// でなくApplySwitchも辿らないと大半のSoundEffectが収集漏れになる)。
void CollectEffectorsRecursive(const PiBlock& container, const std::vector<TriggerStep>& chain,
                               EffectorCollection& out) {
    // Default判定用: このコンテナがApplySwitchなら、宣言済み全Case(非Default)値の
    // 和集合を先に集める(SchemaWalk.cppのwalkApplySwitchと同じ規則)。
    std::vector<int> declaredCaseValues;
    for (const PiMember& m : container.members)
        if (m.kind == PiMember::Kind::CaseLabel && !m.isDefault)
            for (int v : m.caseValues) declaredCaseValues.push_back(v);

    bool haveCase = false;
    std::vector<TriggerStep> caseChain = chain;
    bool wasIf = false;
    std::vector<TriggerStep> elseChain;

    for (const PiMember& member : container.members) {
        if (member.kind == PiMember::Kind::CaseLabel) {
            haveCase = true;
            TriggerStep step;
            step.trigger = TriggerKind::SwitchCase;
            step.switchName = container.label;
            step.switchIsExpression = container.labelIsExpression;
            step.isDefaultCase = member.isDefault;
            if (member.isDefault) step.declaredCaseValues = declaredCaseValues;
            else step.caseValues = member.caseValues;
            caseChain = chain;
            caseChain.push_back(step);
            wasIf = false; // 新しいCase本体区間: If/Elseの対応関係をリセット
            continue;
        }
        const std::vector<TriggerStep>& baseChain = haveCase ? caseChain : chain;
        if (member.kind == PiMember::Kind::Assignment) {
            const PiAssignment& assignment = container.assignments[member.index];
            RailEffectorDef railEffector;
            if (IsRailEffectorKey(assignment.key) &&
                ParseRailEffector(assignment, MakeTriggerInfo(baseChain), railEffector))
                out.railEffectors.push_back(std::move(railEffector));
            continue; // 代入はIf/Elseの対応関係を切らない(ブロックだけが切る)
        }
        if (member.kind != PiMember::Kind::Block) continue;

        const PiBlock& child = container.blocks[member.index];
        if (child.name == "SoundEffect" || child.name == "Headlight" || child.name == "Particle") {
            CollectEffector(child, MakeTriggerInfo(baseChain), out);
            wasIf = false;
            continue;
        }
        if (child.name == "ApplySwitch") {
            CollectEffectorsRecursive(child, baseChain, out);
            wasIf = false;
            continue;
        }
        if (child.name == "If") {
            TriggerStep step;
            step.trigger = TriggerKind::Condition;
            step.condition = child.label;

            std::vector<TriggerStep> ifChain = baseChain;
            ifChain.push_back(step);
            CollectEffectorsRecursive(child, ifChain, out);

            step.negateCondition = true;
            elseChain = baseChain;
            elseChain.push_back(step);
            wasIf = true;
            continue;
        }
        if (child.name == "Else") {
            if (wasIf) CollectEffectorsRecursive(child, elseChain, out);
            wasIf = false;
            continue;
        }
        wasIf = false;
    }
}

} // namespace

void CollectEffectors(const PiBlock& container, EffectorCollection& out) {
    const std::vector<TriggerStep> alwaysChain;
    CollectEffectorsRecursive(container, alwaysChain, out);
}

std::vector<std::string> CollectEffectorLabels(const EffectorCollection& effectors) {
    std::vector<std::string> labels;
    for (const SoundEffectRef& soundEffect : effectors.soundEffects) labels.push_back(soundEffect.attachObject);
    for (const HeadlightDef& headlight : effectors.headlights) labels.push_back(headlight.attachObject);
    for (const ParticleDef& particle : effectors.particles) labels.push_back(particle.attachObject);
    return labels;
}

} // namespace rs::legacy_pi::schema_detail
