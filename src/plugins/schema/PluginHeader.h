//
// PluginHeader.h - プラグインの種別とヘッダ
//
// 種別とヘッダだけを使う側が、定義全体まで読まずに済むよう PluginSchema.h から分けてある。
//
#pragma once

#include <string>

namespace rs::legacy_pi {

enum class PluginType {
    Unknown, Train, Rail, Tie, Girder, Pier, Pole,
    Station, Struct, Surface, Line, Env, Skin
};

const char* PluginTypeName(PluginType t);
PluginType  PluginTypeFromString(const std::string& s);

struct PluginHeader {
    std::string railSimVersion;
    PluginType  type = PluginType::Unknown;
    std::string typeRaw;     // 元の PluginType 文字列
    std::string name;
    std::string author;
    std::string iconTexture; // 未解決の相対パス
    // IconRect = u, v, 幅, 高さ。IconTexture のうちアイコンに使う範囲(0〜1の比率)。省略時は全体。
    // 4値そろわないときは省略扱い(既定値のまま)。
    float       iconRect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    std::string description;
};

} // namespace rs::legacy_pi
