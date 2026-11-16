//
// ZyLinkageSolve.h - ZY系の可動部(ObjectZY/TriangleZY/PistonZY/CrankZY)の姿勢を解く。
//
// RailSim II の CFreeObjectZY/CFreeTriangleZY/CFreeCrankZY/CFreePistonZY の SetPostureFreeObject
// (CTrainPlugin.cpp)の式と同じ。読み込み時の静的な解決
// (SchemaPlacement.cpp の resolveJoint3D)と、毎フレームの再解決
// (DynamicJointPlacement.cpp の ResolveDynamicPartWorlds)の両方が同じ関数を呼ぶ。
//
// 行列の規約は他の配置と同じ: 行優先・行ベクトルの4x4で、
// m[0..2]=right、m[4..6]=up、m[8..10]=forward(dir)、m[12..14]=origin。
//
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "../../core/Math.h"
#include "PluginSchema.h"

namespace rs::legacy_pi {

// JointZYX の取り付け先1つ分。world は取り付け先パーツの今のワールド行列
// (拡大率は頂点側に焼き込み済みのため、RailSim II の拡大率での割り算は行わない)。
struct ZyAttachFrame {
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool  turn = false; // 取り付け先パーツの Turn(右方向を反転して使う)
};

// JointZYXの位置と、取り付け先のTurnを反映した右方向。静的メッシュ生成にも使う。
rs::core::Vec3 JointPos(const ZyAttachFrame& frame, const JointZYXRef& joint);
rs::core::Vec3 JointRight(const ZyAttachFrame& frame);

// グループ内の1パーツ分の入力。attach[i] は info->joints[i] の取り付け先。
struct ZyMemberInput {
    const ZyPartInfo* info = nullptr;
    bool              turn = false; // パーツ自身の Turn
    ZyAttachFrame     attach[2];
};

// 1グループ(ObjectZY は1パーツ、ほかは2パーツ)の入力。
// members は ZyPartInfo::memberIndex の順(Crank は Link→Slide)に並べる。
struct ZyGroupInput {
    ZyMemberInput members[2];
    int           memberCount = 0;
};

// グループの姿勢を解き、outWorld[i] に members[i] のワールド行列を書く。
// 種類と個数が合わない入力なら false(outWorld は変更しない)。
bool SolveZyGroup(const ZyGroupInput& input, float outWorld[2][16]);

// 取り付け先の名前からワールド行列を引く関数。未解決なら false を返す。
using ZyFrameLookup = std::function<bool(const std::string& name, rs::core::Mat4& outFrame)>;

// 解いた結果(グループ全員分)。indices[i] は parts の添字。
struct ZyGroupResult {
    size_t indices[2] = {0, 0};
    int    count = 0;
    float  worlds[2][16];
};

// parts[self] が属するグループを集め、取り付け先のフレームが全部そろっていれば解く。
// 相手が見つからない、または取り付け先が未解決なら false(次のパスへ持ち越す)。
// 2つの配置経路が共通で使う。経路ごとに違うフレームの引き方は lookup で渡す。
bool SolveZyGroupForPart(const std::vector<ModelPart>& parts, size_t self,
                         const ZyFrameLookup& lookup, ZyGroupResult& out);

// 相手や取り付け先の名前が、どのパーツにもシステム名にも当たらないか。
// true なら姿勢の基準が不明(恒等フォールバックになる)。
bool HasUnresolvedZyRef(const std::vector<ModelPart>& parts, const ModelPart& part);

} // namespace rs::legacy_pi
