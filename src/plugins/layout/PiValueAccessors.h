//
// PiValueAccessors.h - PiBlock から値を取り出す共通ヘルパー
//
// Rs2Schema.cpp / RailwayPluginSetSchema.cpp / TrainGroupTemplateSchema.cpp が
// 共有する GetString/GetFloat/GetInt/GetIntArray/GetYesNo の置き場。
//
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../schema/parser/Ast.h"
#include "../../core/Math.h"

namespace rs::legacy_layout {

inline std::string GetString(const rs::legacy_pi::PiBlock& block, const char* key) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return {};
    return a->values[0].AsString();
}

// アドレス値を生文字列のまま取り出す(2^53超の精度問題を避けるため double を
// 経由しない)。
//
// 実データの保存アドレス(16桁hexポインタダンプ)は常に HexAddr で来る。一方
// 手作業/エディタ生成のレイアウトはパディング無しの短い16進連番IDを使い、
// レクサーの文脈非依存な字句規則により Number(純数字、e終端)や
// Ident(文字始まり)としても出現しうる。Address/Link系フィールドの値は用途上
// 常に「識別子」であり数値演算されないため、いずれの種別も生テキストを
// そのままアドレス文字列として採用してよい。
inline std::string AsAddress(const rs::legacy_pi::PiValue& value) {
    switch (value.kind) {
    case rs::legacy_pi::PiValue::Kind::HexAddr:
    case rs::legacy_pi::PiValue::Kind::Number:
    case rs::legacy_pi::PiValue::Kind::Ident:
        return value.text;
    default:
        return {};
    }
}

inline std::string GetAddress(const rs::legacy_pi::PiBlock& block, const char* key) {
    const rs::legacy_pi::PiAssignment* assignment = block.Find(key);
    if (!assignment || assignment->values.empty()) return {};
    return AsAddress(assignment->values[0]);
}

// block.find()は同名キーの最初の1件しか返さないため、PierLink/PoleLinkや
// PointList.Point のように同一ブロック内へ複数回出現するキーは全件をここで拾う。
inline std::vector<const rs::legacy_pi::PiAssignment*> FindAllAssignments(
    const rs::legacy_pi::PiBlock& block, const char* key) {
    std::vector<const rs::legacy_pi::PiAssignment*> assignments;
    for (const rs::legacy_pi::PiAssignment& assignment : block.assignments) {
        if (assignment.key == key) assignments.push_back(&assignment);
    }
    return assignments;
}

inline float GetFloat(const rs::legacy_pi::PiBlock& block, const char* key, float def) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return def;
    return static_cast<float>(a->values[0].AsFloat(def));
}

inline int GetInt(const rs::legacy_pi::PiBlock& block, const char* key, int def) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return def;
    return a->values[0].AsInt(def);
}

// 計算値は double で保持する方針(TargetSpeed/CurrentSpeed/StopTarget等)。
inline double GetDouble(const rs::legacy_pi::PiBlock& block, const char* key, double def) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return def;
    return a->values[0].AsFloat(def);
}

inline rs::core::Vec3 GetVec3(const rs::legacy_pi::PiBlock& block, const char* key) {
    rs::core::Vec3 v{};
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a || a->values.empty()) return v;
    a->values[0].AsVec3(v.x, v.y, v.z);
    return v;
}

// SwitchOption = <値0>, <値1>, ...; を全値そのままint配列にする(DefineSwitch宣言順に
// 位置対応)。キー自体が無ければ空(=既定値0のまま扱う呼び出し側任せ)。
inline std::vector<int32_t> GetIntArray(const rs::legacy_pi::PiBlock& block, const char* key) {
    std::vector<int32_t> out;
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a) return out;
    out.reserve(a->values.size());
    for (const rs::legacy_pi::PiValue& v : a->values) out.push_back(static_cast<int32_t>(v.AsInt(0)));
    return out;
}

// Xxx=yes/no; 形式のフラグ。キー自体が無ければ def を返す。
// 値がある場合は yes(大文字小文字は区別しない)のみ true で、それ以外(誤記・想定外の識別子)は
// 全て false。実在のプラグインに Yes/No と書くものがあるため、大文字小文字は区別しない
// (書き出し側は常に小文字の yes/no のみを出すので、正準データの往復には影響しない)。
// 値なしの単独フラグ代入 "Xxx;" は RailSim II ならパースエラーだが、ここでは安全側に倒して
// yes 扱いにする。
inline bool GetYesNo(const rs::legacy_pi::PiBlock& block, const char* key, bool def) {
    const rs::legacy_pi::PiAssignment* a = block.Find(key);
    if (!a) return def;
    if (a->IsFlag()) return true;
    return a->values[0].IsYes();
}

} // namespace rs::legacy_layout
