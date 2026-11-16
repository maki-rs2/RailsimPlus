//
// TriggerInfo.h - カスタマイザ・エフェクタの発動文脈(スイッチ/条件)の型
//
// PluginSchema.h(各カスタマイザ)と EffectorSchema.h(灯具・パーティクル)が共用する。
//
#pragma once

#include <string>
#include <vector>

namespace rs::legacy_pi {

// 発動条件の種別。ApplySwitch/If は RailSim II(CSwitchApplier)では同一の
// 「経路上の全ノードがANDで決まる再帰木」だが、本実装ではフラットリスト
// アーキテクチャ(part.textureSteps等)と両立させるため、末端ごとに
// 「祖先から自分までの TriggerStep 列(チェーン)」として畳み込んで保持する
// （下記 TriggerInfo 参照）。
enum class TriggerKind { SwitchCase, Condition };

// チェーンの1段分(ApplySwitchの1段、またはIf/Elseの1段)。
struct TriggerStep {
    TriggerKind trigger = TriggerKind::SwitchCase;
    std::string switchName;             // SwitchCase: ApplySwitch のラベル
    // SwitchCase: switchName が式(`ApplySwitch ("_YEAR"/10)%10` 等)なら true。RailSim II は
    // ApplySwitch の後ろを式として読み、その整数値で Case を選ぶ(CSwitchApplier)。
    // false のときは switchName がスイッチ名そのもの。
    bool        switchIsExpression = false;
    std::vector<int> caseValues;        // SwitchCase: `Case n1, n2, ...:` の全値(非Default)
    bool        isDefaultCase = false;  // SwitchCase: Default:
    // SwitchCase(Default専用): 同じ ApplySwitch が宣言する全 Case 値の和集合。
    // walkApplySwitch が sw.members を事前に1パス走査して全ステップへ共通で
    // 埋める。Default の active 判定はこれ単体で完結し(resolve値がこれに
    // 含まれなければactive)、他エントリを横断スキャンする必要がない
    // （宣言された Case の本体が空/別種customizerのみでもDefault判定が壊れない）。
    std::vector<int> declaredCaseValues;
    std::string condition;              // Condition: If の条件式(不透明文字列)
    bool        negateCondition = false; // Condition: Else 側（直前の If 条件を反転）

    // 構造的等価比較（MoverRuntime が「同じ発動条件を持つcustomizerを同じ
    // UIトグルグループへまとめる」際のキー比較に使う。意味的評価ではなく
    // 値の完全一致）。
    bool operator==(const TriggerStep&) const = default;
};

// 発動文脈(スイッチ/条件)。祖先から自分までの TriggerStep の列。
// 空 = Always(無条件)。全ステップをANDで評価する
// （RailSim II の再帰木 CSwitchApplier/CCustomizerSwitchEntry のフラット化表現。
//   末端customizerが発動する条件は「経路上の全ノードの条件が全てtrue」）。
struct TriggerInfo {
    std::vector<TriggerStep> chain;

    bool operator==(const TriggerInfo&) const = default;
};

} // namespace rs::legacy_pi
