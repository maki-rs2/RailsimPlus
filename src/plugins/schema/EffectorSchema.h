//
// EffectorSchema.h - 灯具 Headlight とパーティクル Particle の定義の型
//
// RailSim II では SoundEffect と同じエフェクタの仕組み(CEffector.cpp)で読まれ、PrimaryAssembly 直下のほか
// If / Else / ApplySwitch(Case)の中にも書ける。収集は schema_detail::collectEffectors(SchemaEffectors.cpp)、
// 1件の読み込みは ParseHeadlight / ParseParticle(CustomizerParsers.h)。
// AttachObject は文字列のまま持つ(解決は使う側)。パスも解決しない。
//
#pragma once

#include <array>
#include <optional>
#include <string>

#include "LensFlareSchema.h"
#include "PiColor.h"
#include "TriggerInfo.h"
#include "../../core/Math.h"

namespace rs::legacy_pi {

// Headlight{ AttachObject; SourceCoord; Direction; MaxDistance; LensFlare{} }(RailSim II の CHeadlight::Read)。
struct HeadlightDef {
    std::string    attachObject;   // 必須
    rs::core::Vec3 sourceCoord;    // 必須(AttachObjectのローカル座標)
    rs::core::Vec3 direction;      // 必須
    double         maxDistance = 0.0;  // 必須
    std::optional<LensFlareDef> lensFlare;  // LensFlare{} を省くと、同じプラグインで直前の灯具の値(無ければ空)
    TriggerInfo    trig;           // 発動文脈(既定 Always=空chain)
};

enum class ParticleBlendMode { Alpha, Add };

// Particle{} (RailSim II の CParticle::Read)。
//
// 値は書かれた単位のまま持つ。RailSim II は読み込みのときに次の換算をしている(MAXFPSは RailSim II の定数)。
// 描画を作るときに、使う側で同じ換算をすること。
//   MinQty, MaxQty, Gravity, Turbulence ... / MAXFPS
//   VelocityRel ... * 3.6
//   AccelerationRel, DecelerationRel ... * 3.6 * MAXFPS
//   Direction(2つとも) ... / MAXFPS
//   AirResistance ... 0〜1に収めてから 1 - pow(1 - x, 1 / MAXFPS)
// Lifetime, InitialRadius, FinalRadius, Color は換算なし。
//
// 組の値(Lifetime, Direction, InitialRadius, FinalRadius, Color)は2つまで書け、2つ目を省くと
// 1つ目と同じ値になる(RailSim II の Script.cpp の AsgnFloat 等の fill)。
struct ParticleDef {
    std::string    textureFileName;    // 必須
    std::string    attachObject;       // 必須
    rs::core::Vec3 sourceCoord;        // 必須
    double minQty = 0.0;               // 必須
    double maxQty = 0.0;               // 省略時は minQty と同じ値
    double velocityRel = 0.0;
    double accelerationRel = 0.0;
    double decelerationRel = 0.0;
    std::array<double, 2>         lifetime{};       // 必須
    std::array<rs::core::Vec3, 2> direction{};      // 必須
    std::array<double, 2>         initialRadius{};  // 必須
    std::array<double, 2>         finalRadius{};    // 必須
    std::array<PiColor, 2>        color{};          // 必須
    ParticleBlendMode blendMode = ParticleBlendMode::Alpha;  // 必須
    double airResistance = 0.0;
    double gravity = 0.0;
    double turbulence = 0.0;
    TriggerInfo trig;
};

// 駅のレール接続エフェクタ ConnectRail = n1, n2; / BranchRail = n1, n2; / DisconnectRail = n;
// (RailSim II の CEffectorRailConnector / RailBrancher / RailDisconnector)。代入形式で、読むだけ。
// 端点番号 railEnd は 0 以上のとき platformId = railEnd / 2、platformEnd = railEnd % 2(偶数=始点側)。
// 負の端点番号は取り込むが、platformId/platformEnd は -1/0 のまま。DisconnectRail は railEnd1 だけを使う。
struct RailEffectorDef {
    enum class Kind { Connect, Branch, Disconnect };
    Kind kind = Kind::Connect;
    int railEnd1 = -1;
    int railEnd2 = -1;
    int platformId1 = -1;
    int platformEnd1 = 0;
    int platformId2 = -1;
    int platformEnd2 = 0;
    TriggerInfo trig;
};

} // namespace rs::legacy_pi
