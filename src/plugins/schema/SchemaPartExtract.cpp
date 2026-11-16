//
// SchemaPartExtract.cpp - パーツ直下の組立配置情報（Coord/Turn/Diameter/
// Symmetric/Tilt/JointZY/Joint3D）の抽出。
// collectParts（SchemaWalk.cpp）が各パーツで walkPart（モデル参照/カスタマイザ
// 走査）と対で呼ぶ。抽出した JointZY/Coord/Joint3D 情報の反復解決は
// SchemaPlacement.cpp が別途行う（このファイルはAST→フィールドの抽出のみで、
// 反復パスによる解決は行わない）。
//
#include "SchemaInternal.h"

namespace rs::legacy_pi {

// パーツ直下の組立配置情報（Coord / Turn / JointZY）を抽出する。
void ExtractPlacement(const PiBlock& b, ModelPart& p) {
    if (const PiAssignment* co = b.Find("Coord")) {
        if (co->First() && co->First()->kind == PiValue::Kind::Tuple) {
            const std::vector<PiValue>& t = co->First()->tuple;
            if (t.size() > 0) p.coordZ = static_cast<float>(t[0].AsFloat());
            if (t.size() > 1) p.coordY = static_cast<float>(t[1].AsFloat());
            p.hasCoord = true;
        }
    }
    if (const PiAssignment* tn = b.Find("Turn"))
        p.turn = tn->First() && tn->First()->IsYes();

    // Axle "名" { ...; Diameter=f; } : 車輪直径[m]（走行距離→車輪回転角、0=非回転）
    if (const PiAssignment* d = b.Find("Diameter"))
        if (d->First()) p.diameter = static_cast<float>(d->First()->AsFloat(0.0));

    // Axle "名" { ...; Symmetric=n; } : N角形近似の対称数（逆回転見え防止、既定1）
    if (const PiAssignment* sym = b.Find("Symmetric"))
        if (sym->First()) p.symmetric = static_cast<int>(sym->First()->AsFloat(1.0));

    // Axle "名" { ...; WheelSound=yes-no; } : 継目で車輪音を鳴らすか（省略時yes）
    if (const PiAssignment* ws = b.Find("WheelSound"))
        p.wheelSound = ws->First() && ws->First()->IsYes();

    // Body "名" { ...; Tilt { TiltRatio=f; MaxAngle=f; BaseAlt=f; } } : 振り子傾斜（車体のみ）
    if (const PiBlock* tilt = b.FirstChild("Tilt")) {
        p.tilt.hasTilt = true;
        if (const PiAssignment* a = tilt->Find("TiltRatio"))
            if (a->First()) p.tilt.tiltRatio = static_cast<float>(a->First()->AsFloat(0.0));
        if (const PiAssignment* a = tilt->Find("MaxAngle"))
            if (a->First()) p.tilt.maxAngle = static_cast<float>(a->First()->AsFloat(0.0));
        if (const PiAssignment* a = tilt->Find("BaseAlt"))
            if (a->First()) p.tilt.baseAlt = static_cast<float>(a->First()->AsFloat(0.0));
    }

    // JointZY "子" { AttachCoord=(z,y); LocalCoord=(z,y); } を直下の子から収集。
    // （JointZYX / Joint3D は別機構なので名前一致で自然に除外される）
    for (const auto& c : b.blocks) {
        if (c.name != "JointZY") continue;
        JointRef j;
        j.childLabel = c.label;
        if (const PiAssignment* a = c.Find("AttachCoord"))
            if (a->First() && a->First()->kind == PiValue::Kind::Tuple) {
                const std::vector<PiValue>& t = a->First()->tuple;
                if (t.size() > 0) j.attachZ = static_cast<float>(t[0].AsFloat());
                if (t.size() > 1) j.attachY = static_cast<float>(t[1].AsFloat());
            }
        if (const PiAssignment* a = c.Find("LocalCoord"))
            if (a->First() && a->First()->kind == PiValue::Kind::Tuple) {
                const std::vector<PiValue>& t = a->First()->tuple;
                if (t.size() > 0) j.localZ = static_cast<float>(t[0].AsFloat());
                if (t.size() > 1) j.localY = static_cast<float>(t[1].AsFloat());
            }
        p.joints.push_back(std::move(j));
    }

    // Joint3D "基準名" { AttachCoord=vec3; LocalCoord=vec3(既定0);
    //   DirLink="名"(既定=基準名); AttachDir=vec3(既定+Z);
    //   UpLink="名"(既定=基準名); AttachUp=vec3(既定+Y); }
    for (const auto& c : b.blocks) {
        if (c.name != "Joint3D") continue;
        Joint3DRef j3;
        ParseJoint3DBlock(c, j3);
        p.joints3d.push_back(std::move(j3));
    }
}

// Joint3D "基準名" {...} 1ブロック分をJoint3DRefへパースする（extractPlacementの
// パーツ直下Joint3D抽出ループから抽出。トップレベルFrontCabin/TailCabinの
// パースとロジックを共有する）。
void ParseJoint3DBlock(const PiBlock& c, Joint3DRef& out) {
    out.target = c.label;
    out.dirLink = c.label;
    out.upLink = c.label;
    if (const PiAssignment* a = c.Find("AttachCoord"))
        if (const PiValue* v = a->First())
            v->AsVec3(out.attach[0], out.attach[1], out.attach[2]);
    if (const PiAssignment* a = c.Find("LocalCoord"))
        if (const PiValue* v = a->First())
            v->AsVec3(out.local[0], out.local[1], out.local[2]);
    if (const PiAssignment* a = c.Find("DirLink"))
        if (a->First()) out.dirLink = a->First()->AsString();
    if (const PiAssignment* a = c.Find("AttachDir"))
        if (const PiValue* v = a->First())
            v->AsVec3(out.dir[0], out.dir[1], out.dir[2]);
    if (const PiAssignment* a = c.Find("UpLink"))
        if (a->First()) out.upLink = a->First()->AsString();
    if (const PiAssignment* a = c.Find("AttachUp"))
        if (const PiValue* v = a->First())
            v->AsVec3(out.up[0], out.up[1], out.up[2]);
}

// FrontCabin{ Joint3D "対象" {...} } / TailCabin{ Joint3D "対象" {...} }
// (RailSim II の CTrainPlugin.cpp:589-598)。
// ネストしたJoint3Dブロックが1つも無ければfalseを返す(呼び出し側はフォールバック値を使う)。
bool ParseCabinBlock(const PiBlock& node, Joint3DRef& out) {
    for (const auto& c : node.blocks) {
        if (c.name != "Joint3D") continue;
        ParseJoint3DBlock(c, out);
        return true;
    }
    return false;
}

namespace {

// (z,y) の組を2つの float へ読む。無い・要素不足のときは out を変えない。
void ReadZyPair(const PiBlock& b, const char* key, float& outZ, float& outY) {
    const PiAssignment* a = b.Find(key);
    if (a == nullptr || a->First() == nullptr || a->First()->kind != PiValue::Kind::Tuple) return;
    const std::vector<PiValue>& t = a->First()->tuple;
    if (t.size() > 0) outZ = static_cast<float>(t[0].AsFloat());
    if (t.size() > 1) outY = static_cast<float>(t[1].AsFloat());
}

} // namespace

namespace schema_detail {

// JointZYX "取り付け先"{ AttachX=f; AttachCoord=(z,y); LocalCoord=(z,y); } 1ブロック分。
JointZYXRef ParseJointZYX(const PiBlock& j) {
    JointZYXRef ref;
    ref.target = j.label;
    if (const PiAssignment* a = j.Find("AttachX"))
        if (a->First()) ref.attachX = static_cast<float>(a->First()->AsFloat(0.0));
    ReadZyPair(j, "AttachCoord", ref.attachZ, ref.attachY);
    ReadZyPair(j, "LocalCoord", ref.localZ, ref.localY);
    return ref;
}

} // namespace schema_detail

// ZY系のパーツ(ObjectZY / TriangleZY・PistonZY・CrankZY の Link・Slide)のブロック b から、
// JointZYX・LinkCoord・Direction・FixPosition・FixRight・Turn を抽出する。
// RailSim II の読み込みは CTrainPlugin.cpp の各 Read。
void ExtractZyPartInfo(const PiBlock& b, ZyPartKind kind, int memberIndex, ModelPart& p) {
    p.isZyPart = true;
    p.zyInfo.kind = kind;
    p.zyInfo.memberIndex = memberIndex;
    if (const PiAssignment* tn = b.Find("Turn"))
        p.turn = tn->First() && tn->First()->IsYes();

    const int maxJoints = kind == ZyPartKind::ObjectZY ? 2 : 1;
    for (const auto& c : b.blocks) {
        if (c.name != "JointZYX" || p.zyInfo.jointCount >= maxJoints) continue;
        p.zyInfo.joints[p.zyInfo.jointCount++] = schema_detail::ParseJointZYX(c);
    }

    switch (kind) {
    case ZyPartKind::ObjectZY: {
        // FixPosition の省略時は 0.5、FixRight の省略時は FixPosition と同じ値。
        p.zyInfo.fixPosition = 0.5f;
        if (const PiAssignment* a = b.Find("FixPosition"))
            if (a->First()) p.zyInfo.fixPosition = static_cast<float>(a->First()->AsFloat(0.5));
        p.zyInfo.fixRight = p.zyInfo.fixPosition;
        if (const PiAssignment* a = b.Find("FixRight"))
            if (a->First()) p.zyInfo.fixRight = static_cast<float>(a->First()->AsFloat(0.5));
        break;
    }
    case ZyPartKind::CrankSlide:
        ReadZyPair(b, "Direction", p.zyInfo.directionZ, p.zyInfo.directionY);
        break;
    case ZyPartKind::TriangleLink:
    case ZyPartKind::CrankLink:
    case ZyPartKind::PistonLink:
        ReadZyPair(b, "LinkCoord", p.zyInfo.linkCoordZ, p.zyInfo.linkCoordY);
        break;
    }
}

// FrontCabin/TailCabinブロック省略時のフォールバック。
// RailSim II の CTrainPlugin.cpp:670-683(LoadOldForm)の
// 値は本リポジトリのfront=+Z規約(PluginSchema.h:389「連結位置[m]（front=+Z, tail<front）」)と
// 符号が逆だったため踏襲しない。ここでは本リポジトリ自身の規約に合わせ、FrontCabinは前方
// (+frontLimit)で+Z方向(前方)を、TailCabinは後方(+tailLimit、値自体は負)で-Z方向(後方)を
// 向くように置く。アタッチ先は**末尾のBodyパーツ**(TrainAssembly.h/.cppの
// 「Axle→台車→車体の順（下位が先）に宣言されている標準構成」前提、および
// BuildTrainVehicleTemplateの「フォールバック: どこにも辿り着けなかったパーツは
// 末尾のBody（通常=車体）に委譲する」と同じ規約。先頭のBodyは台車であることが多く、
// 実データ(Bogie1, Bogie2, Dummy=車体)でも車体は末尾)。
void ResolveCabinFallback(TrainInfo& trainInfo, const std::vector<ModelPart>& parts) {
    if (!trainInfo.hasTrainInfo) return;
    if (trainInfo.hasFrontCabin && trainInfo.hasTailCabin) return;

    const ModelPart* lastBody = nullptr;
    for (const ModelPart& part : parts) {
        if (part.kind == "Body") lastBody = &part;
    }
    if (lastBody == nullptr) return;

    if (!trainInfo.hasFrontCabin) {
        Joint3DRef& j = trainInfo.frontCabin;
        j.target = j.dirLink = j.upLink = lastBody->label;
        j.local[0] = j.local[1] = j.local[2] = 0.0f;
        j.attach[0] = 0.0f; j.attach[1] = 2.5f; j.attach[2] = trainInfo.frontLimit;
        j.dir[0] = 0.0f; j.dir[1] = 0.0f; j.dir[2] = 1.0f;
        j.up[0] = 0.0f; j.up[1] = 1.0f; j.up[2] = 0.0f;
        trainInfo.hasFrontCabin = true;
    }
    if (!trainInfo.hasTailCabin) {
        Joint3DRef& j = trainInfo.tailCabin;
        j.target = j.dirLink = j.upLink = lastBody->label;
        j.local[0] = j.local[1] = j.local[2] = 0.0f;
        j.attach[0] = 0.0f; j.attach[1] = 2.5f; j.attach[2] = trainInfo.tailLimit;
        j.dir[0] = 0.0f; j.dir[1] = 0.0f; j.dir[2] = -1.0f;
        j.up[0] = 0.0f; j.up[1] = 1.0f; j.up[2] = 0.0f;
        trainInfo.hasTailCabin = true;
    }
}

} // namespace rs::legacy_pi
