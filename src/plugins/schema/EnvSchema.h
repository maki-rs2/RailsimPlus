//
// EnvSchema.h - Envプラグイン定義(Env2.txt / Env2k.txt)のAST→型付きスキーマ
//
// 既存のPIパーサ(parser/)のASTから、Envの値を取り出す。読むのは次のキーだけ:
//   EnvInfo{ Latitude; EnvMapTexFileName; DiffuseIrradianceTexFileName; SpecularIrradianceTexFileName;
//            BRDFLUTTexFileName }  (後ろ3つはRailSim II -k-build の焼き済みIBL3点セット)
//   Landscape{ ModelFileName; ModelScale }
//   Moon{ RevolutionPeriod; InitialPhase; ModelFileName; ModelScale }  (月の位置と満ち欠け。
//        ModelFileName・ModelScale はプラグインの空が Moon.x を描くとき)
//   Sun{ ModelFileName; ModelScale;
//        Lighting{ NightThreshold; ShadowColor; Set{ SunAlt; Directional; Ambient; SkyColor } }
//        Whiteout{ StartAngle; Color }
//        LensFlare{ StartAngle; Twinkle; Inclination; Circle{} Hexagon{} Texture{} } }
// 読まないもの: Sun.AxialInclination / Moon.AxialInclination(RailSim II の傾きは独自の回転の値で、物理の傾きとは意味が違う)。
//
// パス(`../Default/EnvMap.png` のような親フォルダ参照を含む)は文字列のまま保持する。
// 解決・読込はLandscape・キューブを使う側の仕事(この層では行わない)。
//
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "LensFlareSchema.h"
#include "PiColor.h"
#include "parser/Ast.h"
#include "parser/Diagnostics.h"

namespace rs::legacy_pi {

// Sun.Lighting.Set 1件(太陽高度ごとの光の色。SunAltはsin(太陽高度))。
struct EnvLightingSet {
    double   sunAlt = 0.0;
    PiColor directional;
    PiColor ambient;
    PiColor skyColor;
};

struct EnvWhiteout {
    bool     present = false;
    double   startAngle = 0.0;
    PiColor color;
};

struct EnvDefinition {
    // EnvInfo
    bool        hasLatitude = false;  // Latitudeキーがあった(無ければlatitudeDegは意味を持たない)
    double      latitudeDeg = 0.0;
    std::string envMapTexFileName;    // 空もある
    std::string diffuseIrradianceTexFileName;   // 焼き済みIBLの拡散キューブ。空もある
    std::string specularIrradianceTexFileName;  // 同・鏡面キューブ。空もある
    std::string brdfLutTexFileName;             // 同・BRDF LUT。空もある

    // Landscape
    std::string landscapeModelFileName;  // 空もある
    double      landscapeModelScale = 1.0;

    // Moon(複数あれば最初の1つ。無い・数値でないときは既定値)
    double moonRevolutionPeriodDays = 27.3;
    double moonInitialPhaseDays = 0.0;
    std::string moonModelFileName;  // 空もある
    double      moonModelScale = 1.0;

    // Sun.Lighting
    double   nightThreshold = 0.0;  // 読むだけで使わない(夜判定は環境プリセットが単一の情報源)
    bool     hasShadowColor = false;
    PiColor shadowColor;
    std::vector<EnvLightingSet> lightingSets;  // 宣言順

    // Sun
    std::string sunModelFileName;  // 空もある
    double      sunModelScale = 1.0;
    EnvWhiteout  whiteout;
    std::optional<LensFlareDef> lensFlare;  // LensFlare{} があるときだけ(型は灯具と共用)
};

// ASTからEnvDefinitionを作る。欠けているキーは既定値のまま。値の型や色の桁数が
// おかしいものは、落とさずdiagへ警告として出す。
EnvDefinition BuildEnvDefinition(const PiDocument& doc, Diagnostics& diag);

// 太陽の通り道に使う緯度[度]を選ぶ。envにLatitudeがあればその値、
// 無ければ(envがnullptr=Envなし、Latitudeキー無しを含む)fallbackDegを返す。
// 昼夜判定・季節スイッチ・空の太陽位置は、すべてこの関数の結果を使うこと。
double ResolveSolarLatitudeDeg(const EnvDefinition* env, double fallbackDeg);

} // namespace rs::legacy_pi
