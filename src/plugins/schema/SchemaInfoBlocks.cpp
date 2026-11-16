//
// SchemaInfoBlocks.cpp - トップレベルの情報ブロック(TrainInfo/RailInfo/PierInfo等)の読み取り
//
// BuildPluginDefinition（PluginSchema.cpp）がブロック名ごとに呼ぶ。
// 読んだ値は PluginDefinition の該当フィールドへ直接書く。
//
#include "SchemaInternal.h"

#include <algorithm>

namespace rs::legacy_pi::schema_detail {

namespace {

// keyが数値で書かれているときだけoutへ入れる。AsFloatのfallbackは数値化できないときの値。
void AssignFloat(const PiBlock& node, const char* key, double fallback, float& out) {
    if (const PiAssignment* a = node.Find(key))
        if (a->First()) out = static_cast<float>(a->First()->AsFloat(fallback));
}

void AssignInt(const PiBlock& node, const char* key, double fallback, int& out) {
    if (const PiAssignment* a = node.Find(key))
        if (a->First()) out = static_cast<int>(a->First()->AsFloat(fallback));
}

// keyが"yes"のとき真。キーが無いときはoutを変えない。
void AssignYesNo(const PiBlock& node, const char* key, bool& out) {
    if (const PiAssignment* a = node.Find(key))
        out = a->First() && a->First()->IsYes();
}

// 局所オフセットのベクトルのうちy成分だけを返す。キーが無いときは0。
float ReadLocalY(const PiBlock& node, const char* key) {
    if (const PiAssignment* a = node.Find(key))
        if (const PiValue* v = a->First()) {
            float x = 0, y = 0, z = 0;
            v->AsVec3(x, y, z);
            return y;
        }
    return 0.0f;
}

} // namespace

std::string StrAssign(const PiBlock& b, const char* key) {
    const PiAssignment* a = b.Find(key);
    if (a && a->First() && a->First()->kind == PiValue::Kind::String)
        return a->First()->AsString();
    if (a && a->First()) return a->First()->text;
    return {};
}

void ReadSwitchDef(const PiBlock& node, PluginDefinition& def) {
    SwitchDef sw;
    sw.name = node.label;
    sw.groupCommon = StrAssign(node, "GroupCommon");
    for (const auto& a : node.assignments)
        if (a.key == "Entry" && a.First())
            sw.entries.push_back(a.First()->AsString());
    def.switches.push_back(std::move(sw));
}

void ReadTrainInfo(const PiBlock& node, PluginDefinition& def) {
    TrainInfo& info = def.trainInfo;
    info.hasTrainInfo = true;
    AssignFloat(node, "FrontLimit", 0.0, info.frontLimit);
    AssignFloat(node, "TailLimit", 0.0, info.tailLimit);
    AssignFloat(node, "MaxVelocity", 0.0, info.maxVelocity);
    AssignFloat(node, "MaxAcceleration", 0.0, info.maxAcceleration);
    AssignFloat(node, "MaxDeceleration", 0.0, info.maxDeceleration);
    AssignFloat(node, "TiltSpeed", 0.0, info.tiltSpeed);
    AssignFloat(node, "DoorClosingTime", 0.0, info.doorClosingTime);
}

void ReadRailInfo(const PiBlock& node, PluginDefinition& def) {
    // Rail/Tie/Girderは同じ押し出しメッシュ情報ブロックだが、PI仕様上は
    // ブロック名がプラグイン種別ごとに異なる(RailInfo/TieInfo/GirderInfo)。
    // Height/FlattenCant/SurfaceAltは3種で共通、Gauge/CantRatio/MaxCantは
    // Rail以外では省略され既定値のまま(BuildRailMeshAlongSplineはcantを
    // TrackNodeSample側から得るためTie/Girderには不要)。
    def.hasRailInfo = true;
    AssignFloat(node, "Gauge", 1.435, def.gauge);
    AssignFloat(node, "CantRatio", 0.0, def.cantRatio);
    AssignFloat(node, "MaxCant", 0.0, def.maxCant);
    AssignYesNo(node, "FlattenCant", def.flattenCant);
    AssignFloat(node, "Height", 0.0, def.railHeight);
    AssignFloat(node, "SurfaceAlt", 0.0, def.surfaceAlt);
    if (node.name == "GirderInfo") {
        AssignInt(node, "TrackNum", 1.0, def.trackNum);
        AssignFloat(node, "TrackInterval", 0.0, def.trackInterval);
    }
}

void ReadSurfaceInfo(const PiBlock& node, PluginDefinition& def) {
    const PiAssignment* sx = node.Find("SizeX");
    const PiAssignment* sz = node.Find("SizeZ");
    if (sx && sx->First() && sz && sz->First()) {
        def.hasSurfaceInfo = true;
        def.surfaceSizeX = static_cast<float>(sx->First()->AsFloat(0.0));
        def.surfaceSizeZ = static_cast<float>(sz->First()->AsFloat(0.0));
    }
}

void ReadInterval(const PiBlock& node, PluginDefinition& def) {
    RailInterval iv;
    iv.modelFile = StrAssign(node, "ModelFileName");
    AssignFloat(node, "ModelScale", 1.0, iv.modelScale);
    AssignFloat(node, "Interval", 1.0, iv.interval);
    AssignYesNo(node, "IgnoreCant", iv.ignoreCant);
    AssignFloat(node, "Offset", 0.0, iv.offset);
    iv.wireTracking = StrAssign(node, "WireTracking");
    if (!iv.modelFile.empty()) def.intervals.push_back(std::move(iv));
}

void ReadPierInfo(const PiBlock& node, PluginDefinition& def) {
    AssignFloat(node, "Interval", 20.0, def.pier.interval);
    AssignFloat(node, "BuildMinAlt", 0.0, def.pier.buildMinAlt);
    AssignInt(node, "TrackNum", 1.0, def.pier.trackNum);
    AssignFloat(node, "TrackInterval", 0.0, def.pier.trackInterval);
    // Direction は Up のときだけ上向き。それ以外(Down・省略・不正な値)は下向きとして読む。
    def.pier.directionUp = StrAssign(node, "Direction") == "Up";
    AssignFloat(node, "Offset", 0.0, def.pier.offset);
    AssignFloat(node, "TaperX", 0.0, def.pier.taperX);
    AssignFloat(node, "TaperY", 0.0, def.pier.taperY);
    AssignFloat(node, "TaperZ", 0.0, def.pier.taperZ);
    // テーパは正の値だけ。負の値は広がりなしとして扱う。
    def.pier.taperX = std::max(def.pier.taperX, 0.0f);
    def.pier.taperY = std::max(def.pier.taperY, 0.0f);
    def.pier.taperZ = std::max(def.pier.taperZ, 0.0f);
}

void ReadLineInfo(const PiBlock& node, PluginDefinition& def) {
    def.line.hasLineInfo = true;
    AssignFloat(node, "TrolleyAlt", 0.0, def.line.trolleyAlt);
    AssignFloat(node, "Height", 0.0, def.line.height);
    AssignFloat(node, "MaxInterval", 40.0, def.line.maxInterval);
    AssignFloat(node, "MaxDeflection", 0.5, def.line.maxDeflection);
    AssignFloat(node, "Offset", 0.0, def.line.offset);
}

void ReadPoleInfo(const PiBlock& node, PluginDefinition& def) {
    def.pole.hasPoleInfo = true;
    AssignInt(node, "TrackNum", 1.0, def.pole.trackNum);
    AssignFloat(node, "TrackInterval", 0.0, def.pole.trackInterval);
}

void ReadSoundInfo(const PiBlock& node, PluginDefinition& def) {
    def.railSound.hasSoundInfo = true;
    def.railSound.wheelSoundFile = StrAssign(node, "WheelSoundFile");
    AssignFloat(node, "JointInterval", 0.0, def.railSound.jointInterval);
}

void ReadPierPart(const PiBlock& node, PluginDefinition& def) {
    PierPartRef ref;
    ref.modelFile = StrAssign(node, "ModelFileName");
    AssignFloat(node, "ModelScale", 1.0, ref.modelScale);
    if (node.name == "Joint") {
        ref.localOffsetY = ReadLocalY(node, "JointToHeadLocal");
        def.pier.joint = ref;
        def.pier.hasJoint = true;
    } else if (node.name == "Head") {
        ref.localOffsetY = ReadLocalY(node, "HeadToPierLocal");
        def.pier.head = ref;
        def.pier.hasHead = true;
    } else { // Base
        ref.localOffsetY = ReadLocalY(node, "BaseToPierLocal");
        def.pier.base = ref;
        def.pier.hasBase = true;
    }
}

} // namespace rs::legacy_pi::schema_detail
