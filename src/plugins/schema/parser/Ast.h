//
// Ast.h - PI/.rs2 共通ブロック構文の AST
//
// 出現順を保持する members 列を持つ（ApplySwitch{ Case n: ... } で
// 順序・分岐が意味を持つため）。
//
#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rs::legacy_pi {

struct PiValue {
    enum class Kind { Number, String, HexAddr, Tuple, Ident, HexColor };
    Kind                 kind = Kind::Number;
    double               number = 0.0;  // Number
    std::string          text;          // String / HexAddr / Ident (UTF-8)
    std::vector<PiValue> tuple;         // Tuple

    double AsFloat(double def = 0.0) const {
        return kind == Kind::Number ? number : def;
    }
    int AsInt(int def = 0) const {
        return kind == Kind::Number ? static_cast<int>(number) : def;
    }
    const std::string& AsString() const { return text; }

    // yes/no の識別子を、大文字小文字を区別せずに判定する(Yes・YES・No などの書き方も受ける)。
    bool IsYes() const { return EqualsIgnoreCase("yes"); }
    bool IsNo() const { return EqualsIgnoreCase("no"); }

private:
    bool EqualsIgnoreCase(std::string_view lower) const {
        if (text.size() != lower.size()) return false;
        for (size_t i = 0; i < text.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(text[i])) != lower[i]) return false;
        return true;
    }

public:

    // (x,y,z) タプル or 3連想定。足りない要素は def。
    void AsVec3(float& x, float& y, float& z, float def = 0.0f) const {
        x = y = z = def;
        if (kind != Kind::Tuple) return;
        if (tuple.size() > 0) x = static_cast<float>(tuple[0].AsFloat(def));
        if (tuple.size() > 1) y = static_cast<float>(tuple[1].AsFloat(def));
        if (tuple.size() > 2) z = static_cast<float>(tuple[2].AsFloat(def));
    }
};

struct PiAssignment {
    std::string          key;
    std::vector<PiValue> values; // 空ならフラグ代入 (例: RailCheck;)

    bool IsFlag() const { return values.empty(); }
    const PiValue* First() const { return values.empty() ? nullptr : &values.front(); }
};

struct PiBlock;

// 順序保持のためのタグ付きメンバ
struct PiMember {
    enum class Kind { Block, Assignment, CaseLabel };
    Kind   kind = Kind::Assignment;
    size_t index = 0;    // blocks_ / assignments_ 内のインデックス
    int    caseValue = 0; // CaseLabel のとき（先頭値。互換用）
    std::vector<int> caseValues; // CaseLabel のとき（`Case n1, n2, ...:` の全値）
    bool   isDefault = false; // CaseLabel が Default: のとき
};

struct PiBlock {
    std::string               name;   // ブロック名
    std::string               label;  // Label（無ければ空）
    // label が条件式(If/ApplySwitch の後ろの式をトークン連結した文字列)なら true。
    // false のときは引用符を外した名前1つ(Body "Bogie1"、ApplySwitch "sw" 等)。
    bool                      labelIsExpression = false;
    std::vector<PiBlock>      blocks;
    std::vector<PiAssignment> assignments;
    std::vector<PiMember>     members; // 出現順（Block/Assignment/CaseLabel）

    // --- アクセサ ---
    const PiAssignment* Find(const std::string& key) const {
        for (const auto& a : assignments)
            if (a.key == key) return &a;
        return nullptr;
    }
    std::vector<const PiBlock*> Children(const std::string& blockName) const {
        std::vector<const PiBlock*> out;
        for (const auto& b : blocks)
            if (b.name == blockName) out.push_back(&b);
        return out;
    }
    const PiBlock* FirstChild(const std::string& blockName) const {
        for (const auto& b : blocks)
            if (b.name == blockName) return &b;
        return nullptr;
    }
};

struct PiDocument {
    std::vector<PiBlock> nodes; // トップレベルのブロック列
};

} // namespace rs::legacy_pi
