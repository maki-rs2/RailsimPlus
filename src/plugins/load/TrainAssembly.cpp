#include "../../render/RenderPrecision.h"
#include "TrainAssembly.h"
#include "../schema/PluginSchema.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace rs::legacy_plugin {

using rs::core::InverseRigid;
using rs::core::Mat4;
using rs::core::Vec3;
using rs::legacy_pi::JointRef;
using rs::legacy_pi::ModelPart;
using rs::legacy_pi::PluginDefinition;
using rs::sim::Axle;
using rs::sim::BodyPiece;
using rs::sim::TrainVehicle;
using rs::world::EvaluateAt;
using rs::world::TrackEdge;
using rs::world::TrackFrame;
using rs::world::TrackGraph;
using rs::world::TrackPosition;

using AnchorKind = TrainVehicleTemplate::PartRenderInfo::AnchorKind;

namespace {

Mat4 StaticMatOf(const ModelPart& p) {
    Mat4 m{};
    std::memcpy(m.m, p.worldMat, sizeof(m.m));
    return m;
}

const ModelPart* FindPart(const std::vector<ModelPart>& parts, const std::string& label) {
    for (const auto& p : parts)
        if (p.label == label) return &p;
    return nullptr;
}

// 剛体基底(right,up,forward)+原点からワールド行列を組む(row-major, v'=v*M)。
Mat4 ComposeRigid(const Vec3& origin, const Vec3& forward, const Vec3& up) {
    const Vec3 right = Vec3::Cross(up, forward);
    Mat4 m = Mat4::Identity();
    m.m[0] = right.x;    m.m[1] = right.y;    m.m[2] = right.z;
    m.m[4] = up.x;       m.m[5] = up.y;       m.m[6] = up.z;
    m.m[8] = forward.x;  m.m[9] = forward.y;  m.m[10] = forward.z;
    m.m[12] = origin.x;  m.m[13] = origin.y;  m.m[14] = origin.z;
    return m;
}

// PI の Turn=yes（X/Z軸鏡像）に相当する行列。メッシュ姿勢の合成のみに使う
// （物理解決側のZ符号反転は BuildTrainVehicleTemplate 側で別途処理済み）。
Mat4 TurnMat(bool turn) {
    if (!turn) return Mat4::Identity();
    return Mat4{{-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1}};
}

// 車輪回転: ComposeRigid の基底(row0=right)で言う局所X軸(=right, 車軸の
// 物理的な向き)まわりの回転（RailSim II の CAxleObject::SetPosture の RotX 相当）。
Mat4 RotXMat(double angleRad) {
    const float c = rs::render::ToGpuFloat(std::cos(angleRad));
    const float s = rs::render::ToGpuFloat(std::sin(angleRad));
    return Mat4{{1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1}};
}

// target が Axle/Body 自己アンカーであれば Fix の targetKind/targetIndex を埋める。
bool ResolveFixTarget(const std::vector<TrainVehicleTemplate::PartRenderInfo>& partRender,
                      const std::vector<ModelPart>& parts, const ModelPart* target,
                      BodyPiece::Fix& fix) {
    if (!target) return false;
    const size_t ti = static_cast<size_t>(target - parts.data());
    const TrainVehicleTemplate::PartRenderInfo& tinfo = partRender[ti];
    if (!tinfo.isAnchorSelf) return false;
    fix.targetKind = (tinfo.anchorKind == AnchorKind::Axle) ? BodyPiece::Fix::TargetKind::Axle
                                                             : BodyPiece::Fix::TargetKind::Body;
    fix.targetIndex = tinfo.anchorIndex;
    return true;
}

} // namespace

TrainVehicleTemplate BuildTrainVehicleTemplate(const PluginDefinition& def) {
    TrainVehicleTemplate tmpl;
    tmpl.vehicle.frontLimit = def.trainInfo.frontLimit;
    tmpl.vehicle.tailLimit = def.trainInfo.tailLimit;
    tmpl.vehicle.maxVelocity = def.trainInfo.maxVelocity;
    tmpl.vehicle.maxAcceleration = def.trainInfo.maxAcceleration;
    tmpl.vehicle.maxDeceleration = def.trainInfo.maxDeceleration;
    tmpl.vehicle.tiltSpeed = def.trainInfo.tiltSpeed;
    tmpl.vehicle.doorClosingTime = def.trainInfo.doorClosingTime;
    tmpl.partRender.assign(def.parts.size(), TrainVehicleTemplate::PartRenderInfo{});

    // 1) Axle をそのまま vehicle.axles へ（自己アンカー）。
    for (size_t i = 0; i < def.parts.size(); ++i) {
        const ModelPart& p = def.parts[i];
        if (p.kind != "Axle") continue;
        Axle axle;
        axle.coordZ = p.coordZ;
        axle.coordY = p.coordY;
        axle.diameter = p.diameter;
        axle.symmetric = p.symmetric;
        axle.wheelSound = p.wheelSound;
        tmpl.vehicle.axles.push_back(axle);
        tmpl.axlePartIndex.push_back(static_cast<int>(i));

        TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[i];
        info.anchorKind = AnchorKind::Axle;
        info.anchorIndex = static_cast<int>(tmpl.vehicle.axles.size()) - 1;
        info.isAnchorSelf = true;
    }

    // 2) 2本の JointZY を持つ Body を vehicle.bodies へ（自己アンカー）。
    // 前提: 参照先(Axle/Body)は既にこのループ内でより前の添字として処理済み
    // （宣言順=依存順の標準構成）。
    for (size_t i = 0; i < def.parts.size(); ++i) {
        const ModelPart& p = def.parts[i];
        if (p.kind != "Body" || p.joints.size() != 2) continue;

        const ModelPart* t0 = FindPart(def.parts, p.joints[0].childLabel);
        const ModelPart* t1 = FindPart(def.parts, p.joints[1].childLabel);
        if (!t0 || !t1) continue; // 未知ラベル参照は追従対象外のまま(Object3D経路へ)

        // 前後判定: 宣言順ではなく resolvePlacements 済みの絶対 offsetZ で判定する
        // （このプラグインでも Bogie1/Bogie2/MainBody は前→後の宣言順で、
        //  fix0=後方/fix1=前方の規約と逆になるため）。
        const bool zeroIsRear = t0->offsetZ <= t1->offsetZ;
        const ModelPart* pRear = zeroIsRear ? t0 : t1;
        const ModelPart* pFront = zeroIsRear ? t1 : t0;
        const JointRef& jRear = zeroIsRear ? p.joints[0] : p.joints[1];
        const JointRef& jFront = zeroIsRear ? p.joints[1] : p.joints[0];

        BodyPiece piece;
        // Turn=yes: 自身のローカルZ（JointZY.LocalCoord.z）の符号を反転する
        // （resolvePlacements の localZeff と同じ規約。Turn はZ軸鏡像）。
        const float localZSign = p.turn ? -1.0f : 1.0f;
        piece.fix0.attachZ = jRear.attachZ;
        piece.fix0.attachY = jRear.attachY;
        piece.fix0.localZ = jRear.localZ * localZSign;
        piece.fix0.localY = jRear.localY;
        piece.fix1.attachZ = jFront.attachZ;
        piece.fix1.attachY = jFront.attachY;
        piece.fix1.localZ = jFront.localZ * localZSign;
        piece.fix1.localY = jFront.localY;

        // 振り子傾斜(Tilt, 車体のみ opt-in)。MaxAngleは[deg]表記のため
        // rad へ変換して保持する。
        piece.hasTilt = p.tilt.hasTilt;
        piece.tiltRatio = p.tilt.tiltRatio;
        piece.tiltMaxAngle = p.tilt.maxAngle * std::numbers::pi / 180.0;
        piece.tiltBaseAlt = p.tilt.baseAlt;

        if (!ResolveFixTarget(tmpl.partRender, def.parts, pRear, piece.fix0) ||
            !ResolveFixTarget(tmpl.partRender, def.parts, pFront, piece.fix1))
            continue; // 依存先が未解決(宣言順が前提と異なる)。追従対象外のまま。

        tmpl.vehicle.bodies.push_back(piece);
        tmpl.bodyPartIndex.push_back(static_cast<int>(i));
        tmpl.bodyTurn.push_back(p.turn);

        TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[i];
        info.anchorKind = AnchorKind::Body;
        info.anchorIndex = static_cast<int>(tmpl.vehicle.bodies.size()) - 1;
        info.isAnchorSelf = true;
    }

    // 3) 残りのパーツ(Object3D等)は Joint3D の参照先をたどり、自己アンカー
    // (Axle/Body)に行き着けば相対デルタで追従させる。反復解決
    // （resolveJoint3D と同じパターン: 依存先が先に解決されるまで据え置く）。
    for (size_t pass = 0; pass < def.parts.size() + 1; ++pass) {
        bool progressed = false;
        for (size_t i = 0; i < def.parts.size(); ++i) {
            TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[i];
            if (info.anchorKind != AnchorKind::None) continue;
            const ModelPart& p = def.parts[i];
            if (p.joints3d.empty()) continue;
            const ModelPart* target = FindPart(def.parts, p.joints3d.back().target);
            if (!target) continue; // _LOCAL/_WORLD等 or 未知参照。フォールバックへ後段で回す。
            const size_t ti = static_cast<size_t>(target - def.parts.data());
            const TrainVehicleTemplate::PartRenderInfo& tinfo = tmpl.partRender[ti];
            if (tinfo.anchorKind == AnchorKind::None) continue; // ターゲット未解決。次パスへ。

            // 中継パーツ(target自身がObject3D等)であっても、その target が
            // 最終的に辿り着いた自己アンカーそのものの静的配置を基準に取る
            // （target の worldMat は既に絶対解決済みなので中継経由でも1段で足りる）。
            const int governedPartIdx = (tinfo.anchorKind == AnchorKind::Axle)
                ? tmpl.axlePartIndex[static_cast<size_t>(tinfo.anchorIndex)]
                : tmpl.bodyPartIndex[static_cast<size_t>(tinfo.anchorIndex)];
            const Mat4 anchorStatic = StaticMatOf(def.parts[static_cast<size_t>(governedPartIdx)]);

            info.anchorKind = tinfo.anchorKind;
            info.anchorIndex = tinfo.anchorIndex;
            info.isAnchorSelf = false;
            info.deltaMat = Mat4::Mul(StaticMatOf(p), InverseRigid(anchorStatic));
            progressed = true;
        }
        if (!progressed) break;
    }

    // フォールバック: どこにも辿り着けなかったパーツは末尾の Body（通常=車体）に委譲する。
    if (!tmpl.bodyPartIndex.empty()) {
        const int fallbackPartIdx = tmpl.bodyPartIndex.back();
        const int fallbackBodyIdx = static_cast<int>(tmpl.bodyPartIndex.size()) - 1;
        const Mat4 fallbackStatic = StaticMatOf(def.parts[static_cast<size_t>(fallbackPartIdx)]);
        for (size_t i = 0; i < def.parts.size(); ++i) {
            TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[i];
            if (info.anchorKind != AnchorKind::None) continue;
            info.anchorKind = AnchorKind::Body;
            info.anchorIndex = fallbackBodyIdx;
            info.isAnchorSelf = false;
            info.deltaMat = Mat4::Mul(StaticMatOf(def.parts[i]), InverseRigid(fallbackStatic));
        }
    }

    return tmpl;
}

std::vector<Mat4> SolvePartWorlds(const TrackGraph& graph, const TrackPosition& head,
                                   const TrainVehicleTemplate& tmpl, double dt, double dsMeters,
                                   AxleSpinState* axleSpinState, rs::sim::TiltDriverState* tiltState,
                                   double tiltKc, double tiltKr, double* maxTiltAngleOut,
                                   bool reversed, const rs::sim::TrainOccupiedPath* occupiedPath,
                                   double pathBaseOffsetM,
                                   std::vector<AxleContact>* axleContactsOut) {
    TrainVehicle vehicle = tmpl.vehicle; // 実行時状態(on/posW等)を都度書くのでコピー
    // reversed=trueをrs::sim::TrainVehicle::flippedへそのまま橋渡しする。反転の
    // 実際の解決(chaseオフセットの反転+forward軸のその場180°回転+車輪回転符号
    // 反転)はSolveAxles/SolveBodies/SpinAxles側(TrainVehicle.cpp)で行う。
    // (解決後に車両中心まわりの剛体回転を後段適用する方式は、曲線上では反転後の
    // 点が同じ曲線に乗らず車体が外側へずれるため使わない。各パーツを個別に
    // 正しいchaseオフセットで解決する)。
    vehicle.flipped = reversed;
    // spinFraction(車輪回転位相)だけは呼び出し側の永続状態から持ち込む。
    // テンプレのコピーは常にspinFraction=0のため、ここで復元しないと毎回巻き戻る
    // (tiltAngleと違い、Tilt駆動ベクトルのような外部ポインタ直渡しではなく
    // 各axleのスカラー値そのものを持ち越す方式の方が自然なため、tiltState登場前の
    // 初期実装と同じ「復元→ソルブ→書き戻し」パターンを踏襲する)。
    const bool useSpinState = axleSpinState && axleSpinState->size() == vehicle.axles.size();
    if (useSpinState) {
        for (size_t i = 0; i < vehicle.axles.size(); ++i)
            vehicle.axles[i].spinFraction = (*axleSpinState)[i];
    }
    // tiltState(駆動ベクトルの永続状態)は車両単位で呼び出し側が保持するポインタ
    // なので、ここではそのまま SolveTrainVehicle へ橋渡しするだけでよい
    // （tiltAngle自体はテンプレコピーのたび再計算されるだけで持ち越し不要）。
    const rs::sim::GraphForScene graphForPath =
        [&graph, occupiedPath](size_t sceneIndex) -> const TrackGraph* {
            return occupiedPath != nullptr && !occupiedPath->entries.empty() &&
                   sceneIndex == occupiedPath->entries.front().sceneIndex ? &graph : nullptr;
        };
    rs::sim::SolveTrainVehicle(graph, head, vehicle, dt, dsMeters, tiltKc, tiltKr, tiltState,
                               occupiedPath, &graphForPath, pathBaseOffsetM);
    if (useSpinState) {
        for (size_t i = 0; i < vehicle.axles.size(); ++i)
            (*axleSpinState)[i] = vehicle.axles[i].spinFraction;
    }
    if (maxTiltAngleOut) {
        double maxAngle = 0.0;
        for (const auto& b : vehicle.bodies)
            if (b.hasTilt) maxAngle = std::max(maxAngle, std::fabs(b.tiltAngle));
        *maxTiltAngleOut = maxAngle;
    }

    if (axleContactsOut != nullptr) axleContactsOut->clear();
    std::vector<Mat4> worldByAxle(vehicle.axles.size());
    for (size_t i = 0; i < vehicle.axles.size(); ++i) {
        const Axle& axle = vehicle.axles[i];
        const TrackEdge& edge = graph.edges.at(axle.on.edge);
        const TrackFrame f = EvaluateAt(edge, axle.on.s, axle.on.direction);
        // 高さ基準は TrainVehicle.cpp の AxleFrame と完全に一致させること。
        // spline(地面基準)からレール踏面までの持ち上げ量 edge.railTopLift を
        // 忘れると、車軸メッシュだけがその分レールへめり込む(台車/車体は
        // BodyPiece::posW 経由なので正しく、車輪だけ沈む症状になる)。
        const Vec3 origin =
            f.pos + f.up * rs::render::ToGpuFloat(axle.coordY + edge.railTopLift);
        if (axleContactsOut != nullptr) {
            AxleContact contact;
            contact.on = axle.on;
            contact.worldPos = f.pos + f.up * static_cast<float>(edge.railTopLift);
            contact.chaseOffsetM = rs::sim::AxleChaseOffset(vehicle, axle);
            contact.wheelSound = axle.wheelSound;
            axleContactsOut->push_back(contact);
        }
        // reversed時はforwardをその場(位置は不変)で反転する(TrainVehicle.cppの
        // AxleFrameと同じ規約。SolveBodies側の姿勢計算と描画側のこのメッシュ
        // 配置を一致させる必要があるため、ここでも同じ判定を独立に行う)。
        const Vec3 forward = reversed ? f.tangent * -1.0f : f.tangent;
        const Mat4 rigid = ComposeRigid(origin, forward, f.up); // Axle自体はTurn非対応
        // 車輪回転: 局所X(=right)軸まわりに spinFraction*360° 回転させて
        // から配置する(RailSim IIと同じくメッシュ自身のローカル回転として合成)。
        worldByAxle[i] = axle.diameter > 0.0
            ? Mat4::Mul(RotXMat(axle.spinFraction * 2.0 * std::numbers::pi), rigid)
            : rigid;
    }

    std::vector<Mat4> worldByBody(vehicle.bodies.size());
    for (size_t i = 0; i < vehicle.bodies.size(); ++i) {
        const BodyPiece& b = vehicle.bodies[i];
        const Mat4 rigid = ComposeRigid(b.posW, b.forwardW, b.upW);
        worldByBody[i] = Mat4::Mul(TurnMat(tmpl.bodyTurn[i]), rigid);
    }

    std::vector<Mat4> out(tmpl.partRender.size(), Mat4::Identity());
    for (size_t i = 0; i < tmpl.partRender.size(); ++i) {
        const TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[i];
        Mat4 anchorWorld = Mat4::Identity();
        if (info.anchorKind == AnchorKind::Axle)
            anchorWorld = worldByAxle[static_cast<size_t>(info.anchorIndex)];
        else if (info.anchorKind == AnchorKind::Body)
            anchorWorld = worldByBody[static_cast<size_t>(info.anchorIndex)];
        else
            continue; // アンカー不明(車体すら無い等の縮退プラグイン): 恒等のまま

        out[i] = info.isAnchorSelf ? anchorWorld : Mat4::Mul(info.deltaMat, anchorWorld);
    }

    return out;
}

bool FindPartPlacementToWorld(const PluginDefinition& def, const TrainVehicleTemplate& tmpl,
                              const std::vector<Mat4>& solved, size_t partIndex, Mat4& out) {
    if (partIndex >= tmpl.partRender.size()) return false;
    const TrainVehicleTemplate::PartRenderInfo& info = tmpl.partRender[partIndex];
    if (info.anchorKind == AnchorKind::None) return false;
    const size_t anchorPart = static_cast<size_t>(
        info.anchorKind == AnchorKind::Axle
            ? tmpl.axlePartIndex[static_cast<size_t>(info.anchorIndex)]
            : tmpl.bodyPartIndex[static_cast<size_t>(info.anchorIndex)]);
    out = Mat4::Mul(InverseRigid(StaticMatOf(def.parts[anchorPart])), solved[anchorPart]);
    return true;
}

std::vector<Mat4> ComposeDynamicPartWorlds(const PluginDefinition& def,
                                           const TrainVehicleTemplate& tmpl,
                                           const std::vector<Mat4>& solved,
                                           const std::vector<Mat4>& dynamicWorlds) {
    std::vector<Mat4> out = solved;
    const size_t n = std::min({tmpl.partRender.size(), solved.size(), dynamicWorlds.size()});
    for (size_t i = 0; i < n; ++i) {
        Mat4 placement{};
        if (!FindPartPlacementToWorld(def, tmpl, solved, i, placement)) continue;
        // アンカーの静的配置からの相対を、今フレームの解き直し結果で取り直す。
        // アンカー自身は「自分の可動部 × 物理姿勢」になる。
        out[i] = Mat4::Mul(dynamicWorlds[i], placement);
    }
    return out;
}

} // namespace rs::legacy_plugin
