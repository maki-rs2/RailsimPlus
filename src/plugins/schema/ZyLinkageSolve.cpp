//
// ZyLinkageSolve.cpp - ZY系の可動部の姿勢解法(ZyLinkageSolve.h)。
//
// 式は RailSim II の CTrainPlugin.cpp の各クラスの SetPostureFreeObject と同じ順で書いている。
//   CFreeObjectZY::SetPostureFreeObject  -> SolveObjectZy
//   CFreeTriangleZY / CFreePistonZY      -> SolveTriangle / SolvePiston(後半は SolveTwoLinks が共通)
//   CFreeCrankZY                         -> SolveCrank
// RailSim II の V3Cross(&out, &a, &b) は out = a x b で、rs::core::Vec3::cross(a, b) と同じ。
//
#include "ZyLinkageSolve.h"

#include <cmath>

namespace rs::legacy_pi {

namespace {

using rs::core::Vec3;

// 解なしや縮退した入力で NaN を出さないための下限。
constexpr float kZyEpsilon = 1e-8f;

Vec3 RightOf(const float m[16]) { return {m[0], m[1], m[2]}; }
Vec3 UpOf(const float m[16]) { return {m[4], m[5], m[6]}; }
Vec3 DirOf(const float m[16]) { return {m[8], m[9], m[10]}; }
Vec3 OriginOf(const float m[16]) { return {m[12], m[13], m[14]}; }
Vec3 Negated(const Vec3& v) { return v * -1.0f; }

} // namespace

// RailSim II の CObjectJointZYX::GetPos。取り付け先の行列 x (AttachX, AttachCoord.y, AttachCoord.x)。
// RailSim II は拡大率で割るが、こちらの行列には拡大率が入っていないので割らない。
Vec3 JointPos(const ZyAttachFrame& f, const JointZYXRef& j) {
    return OriginOf(f.world) + RightOf(f.world) * j.attachX + UpOf(f.world) * j.attachY +
           DirOf(f.world) * j.attachZ;
}

// RailSim II の CObjectJointZY::GetRight。取り付け先パーツが Turn なら反転する。
Vec3 JointRight(const ZyAttachFrame& f) {
    const Vec3 r = RightOf(f.world);
    return f.turn ? Negated(r) : r;
}

namespace {

// RailSim II の CObject::SetDir(dir, up) の結果。dir と up を正規化し、right = cross(up, dir)、
// up を cross(dir, right) で直交化する。
struct Axes {
    Vec3 right, up, dir;
};

Axes MakeAxes(const Vec3& dirIn, const Vec3& upIn) {
    Axes a;
    a.dir = dirIn.Normalized();
    if (a.dir.Length() < 0.5f) a.dir = {0.0f, 0.0f, 1.0f};
    const Vec3 upN = upIn.Normalized();
    a.right = Vec3::Cross(upN, a.dir).Normalized();
    if (a.right.Length() < 0.5f) {
        // up と dir が平行(または up が零)。dir に垂直な軸を作っておかしくなるのを避ける。
        const Vec3 ref =
            std::fabs(a.dir.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        a.right = Vec3::Cross(ref, a.dir).Normalized();
    }
    a.up = Vec3::Cross(a.dir, a.right).Normalized();
    return a;
}

void WriteWorld(const Axes& a, const Vec3& origin, float out[16]) {
    out[0] = a.right.x;  out[1] = a.right.y;  out[2] = a.right.z;  out[3] = 0.0f;
    out[4] = a.up.x;     out[5] = a.up.y;     out[6] = a.up.z;     out[7] = 0.0f;
    out[8] = a.dir.x;    out[9] = a.dir.y;    out[10] = a.dir.z;   out[11] = 0.0f;
    out[12] = origin.x;  out[13] = origin.y;  out[14] = origin.z;  out[15] = 1.0f;
}

// RailSim II 共通の「辺の向き」: ldir = sdir.x*a - (Turn ? -sdir.y : sdir.y)*b、lup = ldir x right、
// SetDir(ldir, Turn ? -lup : lup)。a は辺の方向、b はそれに直交する方向。
// sdir の x は Z 成分、y は Y 成分(VEC2 の規約)。
Axes LinkAxes(float sdirZ, float sdirY, const Vec3& a, const Vec3& b, const Vec3& right,
              bool turn) {
    const Vec3 ldir = a * sdirZ - b * (turn ? -sdirY : sdirY);
    const Vec3 lup = Vec3::Cross(ldir, right);
    return MakeAxes(ldir, turn ? Negated(lup) : lup);
}

// ObjectZY: 2つの取り付け先の間に FixPosition / FixRight の内分比で置く。
void SolveObjectZy(const ZyMemberInput& m, float out[16]) {
    const ZyPartInfo& info = *m.info;
    const JointZYXRef& jr1 = info.joints[0];
    const JointZYXRef& jr2 = info.joints[1];
    const float sdirZ = jr2.localZ - jr1.localZ;
    const float sdirY = jr2.localY - jr1.localY;
    const Vec3 j1 = JointPos(m.attach[0], jr1);
    const Vec3 j2 = JointPos(m.attach[1], jr2);
    const Vec3 right = (JointRight(m.attach[0]) * (1.0f - info.fixRight) +
                        JointRight(m.attach[1]) * info.fixRight)
                           .Normalized();
    const Vec3 wdir = (j2 - j1).Normalized();
    const Vec3 cpos = j1 * (1.0f - info.fixPosition) + j2 * info.fixPosition;
    const Vec3 w90 = Vec3::Cross(wdir, right).Normalized();
    const Axes axes = LinkAxes(sdirZ, sdirY, wdir, w90, right, m.turn);
    const float difZ = (1.0f - info.fixPosition) * jr1.localZ + info.fixPosition * jr2.localZ;
    const float difY = (1.0f - info.fixPosition) * jr1.localY + info.fixPosition * jr2.localY;
    WriteWorld(axes, cpos - axes.dir * difZ - axes.up * difY, out);
}

// Link の「LinkCoord - LocalCoord」(辺の向きと長さ)。
struct LinkSpan {
    float z, y;
    float Length() const { return std::sqrt(z * z + y * y); }
};

LinkSpan SpanOf(const ZyPartInfo& info) {
    return {info.linkCoordZ - info.joints[0].localZ, info.linkCoordY - info.joints[0].localY};
}

// TriangleZY と PistonZY の共通の後半。2つの辺(Link)が点 ppos で出会うように向ける。
void SolveTwoLinks(const ZyMemberInput& l1, const ZyMemberInput& l2, const Vec3& j1,
                   const Vec3& j2, const Vec3& ppos, const Vec3& right, float out[2][16]) {
    const JointZYXRef& jr1 = l1.info->joints[0];
    const JointZYXRef& jr2 = l2.info->joints[0];
    const LinkSpan s1 = SpanOf(*l1.info);
    const LinkSpan s2 = SpanOf(*l2.info);
    const Vec3 w11 = (ppos - j1).Normalized();
    const Vec3 w12 = Vec3::Cross(w11, right).Normalized();
    const Vec3 w21 = (ppos - j2).Normalized();
    const Vec3 w22 = Vec3::Cross(w21, right).Normalized();
    const Axes a1 = LinkAxes(s1.z, s1.y, w11, w12, right, l1.turn);
    const Axes a2 = LinkAxes(s2.z, s2.y, w21, w22, right, l2.turn);
    WriteWorld(a1, j1 - a1.dir * jr1.localZ - a1.up * jr1.localY, out[0]);
    WriteWorld(a2, j2 - a2.dir * jr2.localZ - a2.up * jr2.localY, out[1]);
}

// TriangleZY: 2つの取り付け先と各辺の長さから三角形の頂点 ppos を求める。
void SolveTriangle(const ZyMemberInput& l1, const ZyMemberInput& l2, float out[2][16]) {
    const Vec3 j1 = JointPos(l1.attach[0], l1.info->joints[0]);
    const Vec3 j2 = JointPos(l2.attach[0], l2.info->joints[0]);
    Vec3 wdir = j2 - j1;
    const Vec3 right = (JointRight(l1.attach[0]) + JointRight(l2.attach[0])).Normalized();
    const float slen1 = SpanOf(*l1.info).Length();
    const float slen2 = SpanOf(*l2.info).Length();
    const float wlen = wdir.Length();
    wdir = wdir.Normalized();
    const Vec3 w90 = Vec3::Cross(wdir, right).Normalized();
    const float px =
        wlen > kZyEpsilon ? (wlen * wlen + slen1 * slen1 - slen2 * slen2) * 0.5f / wlen : 0.0f;
    const float py2 = slen1 * slen1 - px * px;
    // RailSim II と同じく、三角形が成り立たないとき(py2 < 0)は py = 0.5 * wlen。
    const float py = py2 < 0.0f ? 0.5f * wlen : -std::sqrt(py2);
    const Vec3 ppos = j1 + wdir * px + w90 * py;
    SolveTwoLinks(l1, l2, j1, j2, ppos, right, out);
}

// PistonZY: 2つの取り付け先の中点を頂点にする。
void SolvePiston(const ZyMemberInput& l1, const ZyMemberInput& l2, float out[2][16]) {
    const Vec3 j1 = JointPos(l1.attach[0], l1.info->joints[0]);
    const Vec3 j2 = JointPos(l2.attach[0], l2.info->joints[0]);
    const Vec3 right = (JointRight(l1.attach[0]) + JointRight(l2.attach[0])).Normalized();
    SolveTwoLinks(l1, l2, j1, j2, (j1 + j2) * 0.5f, right, out);
}

// CrankZY: Link の円(半径 |LinkCoord - LocalCoord|)と Slide の直線の交点に Slide を置く。
void SolveCrank(const ZyMemberInput& link, const ZyMemberInput& slide, float out[2][16]) {
    const ZyPartInfo& si = *slide.info;
    // RailSim II の CObjectJoint::GetDir/GetUp は取り付け先オブジェクトの軸(Turn の反転なし)。
    const Vec3 sldir = DirOf(slide.attach[0].world);
    const Vec3 slup = UpOf(slide.attach[0].world);
    const Vec3 direction = (sldir * si.directionZ + slup * si.directionY).Normalized();
    const JointZYXRef& lj = link.info->joints[0];
    const LinkSpan s1 = SpanOf(*link.info);
    const Vec3 j1 = JointPos(link.attach[0], lj);
    const Vec3 j2 = JointPos(slide.attach[0], si.joints[0]);
    const Vec3 wdir0 = j2 - j1;
    const Vec3 right = (JointRight(link.attach[0]) + JointRight(slide.attach[0])).Normalized();
    const float lldcos = Vec3::Dot(wdir0, direction);
    const float lldsin = (wdir0 - direction * lldcos).Length();
    const float slen1 = s1.Length();
    const float llofs = slen1 * slen1 - lldsin * lldsin;
    // RailSim II と同じく、円が直線に届かないとき(llofs < 0)は平方根の項を0にする。
    const Vec3 lljpos = j2 + direction * ((llofs < 0.0f ? 0.0f : std::sqrt(llofs)) - lldcos);
    const Vec3 wdir = (lljpos - j1).Normalized();
    const Vec3 w90 = Vec3::Cross(wdir, right).Normalized();
    const Axes a1 = LinkAxes(s1.z, s1.y, wdir, w90, right, link.turn);
    const Vec3 lup2 = sldir * -si.directionY + slup * si.directionZ;
    const Axes a2 = MakeAxes(direction, slide.turn ? Negated(lup2) : lup2);
    WriteWorld(a1, j1 - a1.dir * lj.localZ - a1.up * lj.localY, out[0]);
    WriteWorld(a2, lljpos, out[1]);
}

bool InputIsConsistent(const ZyGroupInput& in) {
    if (in.memberCount < 1 || in.memberCount > 2) return false;
    for (int i = 0; i < in.memberCount; ++i)
        if (in.members[i].info == nullptr) return false;
    const ZyPartKind k0 = in.members[0].info->kind;
    if (k0 == ZyPartKind::ObjectZY)
        return in.memberCount == 1 && in.members[0].info->jointCount == 2;
    if (in.memberCount != 2) return false;
    for (int i = 0; i < 2; ++i)
        if (in.members[i].info->jointCount != 1) return false;
    const ZyPartKind k1 = in.members[1].info->kind;
    if (k0 == ZyPartKind::CrankLink) return k1 == ZyPartKind::CrankSlide;
    return (k0 == ZyPartKind::TriangleLink || k0 == ZyPartKind::PistonLink) && k1 == k0;
}

const ModelPart* FindPartByLabel(const std::vector<ModelPart>& parts, const std::string& label) {
    for (const ModelPart& p : parts)
        if (p.label == label) return &p;
    return nullptr;
}

} // namespace

bool SolveZyGroup(const ZyGroupInput& input, float outWorld[2][16]) {
    if (!InputIsConsistent(input)) return false;
    switch (input.members[0].info->kind) {
    case ZyPartKind::ObjectZY:
        SolveObjectZy(input.members[0], outWorld[0]);
        return true;
    case ZyPartKind::TriangleLink:
        SolveTriangle(input.members[0], input.members[1], outWorld);
        return true;
    case ZyPartKind::PistonLink:
        SolvePiston(input.members[0], input.members[1], outWorld);
        return true;
    case ZyPartKind::CrankLink:
        SolveCrank(input.members[0], input.members[1], outWorld);
        return true;
    case ZyPartKind::CrankSlide:
        break; // Link が先頭に来ていない
    }
    return false;
}

bool SolveZyGroupForPart(const std::vector<ModelPart>& parts, size_t self,
                         const ZyFrameLookup& lookup, ZyGroupResult& out) {
    const ModelPart& me = parts[self];
    if (!me.isZyPart) return false;

    // グループの顔ぶれ(memberIndex の順)。
    size_t order[2] = {self, self};
    int count = 1;
    if (!me.zyInfo.partnerLabel.empty()) {
        size_t partnerIdx = parts.size();
        for (size_t k = 0; k < parts.size(); ++k)
            if (k != self && parts[k].isZyPart && parts[k].label == me.zyInfo.partnerLabel) {
                partnerIdx = k;
                break;
            }
        if (partnerIdx == parts.size()) return false;
        count = 2;
        if (me.zyInfo.memberIndex == 0) {
            order[1] = partnerIdx;
        } else {
            order[0] = partnerIdx;
            order[1] = self;
        }
    }

    ZyGroupInput input;
    input.memberCount = count;
    for (int i = 0; i < count; ++i) {
        const ModelPart& part = parts[order[i]];
        ZyMemberInput& member = input.members[i];
        member.info = &part.zyInfo;
        member.turn = part.turn;
        for (int j = 0; j < part.zyInfo.jointCount && j < 2; ++j) {
            const std::string& target = part.zyInfo.joints[j].target;
            rs::core::Mat4 frame{};
            if (!lookup(target, frame)) return false; // 取り付け先が未解決
            for (int e = 0; e < 16; ++e) member.attach[j].world[e] = frame.m[e];
            const ModelPart* targetPart = FindPartByLabel(parts, target);
            member.attach[j].turn = targetPart != nullptr && targetPart->turn;
        }
    }

    if (!SolveZyGroup(input, out.worlds)) return false;
    out.count = count;
    out.indices[0] = order[0];
    out.indices[1] = order[1];
    return true;
}

bool HasUnresolvedZyRef(const std::vector<ModelPart>& parts, const ModelPart& part) {
    const ZyPartInfo& info = part.zyInfo;
    if (!info.partnerLabel.empty() && FindPartByLabel(parts, info.partnerLabel) == nullptr)
        return true;
    for (int j = 0; j < info.jointCount && j < 2; ++j) {
        const std::string& target = info.joints[j].target;
        if (!IsSystemJointTargetName(target) && FindPartByLabel(parts, target) == nullptr)
            return true;
    }
    return false;
}

} // namespace rs::legacy_pi
