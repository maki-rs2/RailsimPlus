#include "SchemaInternal.h"

namespace rs::legacy_pi::schema_detail {
namespace {
bool IsYes(const PiAssignment* assignment, size_t index, bool defaultValue) {
    if (assignment == nullptr || index >= assignment->values.size()) return defaultValue;
    return assignment->values[index].IsYes();
}

std::string StringAssignment(const PiBlock& node, const char* key) {
    const PiAssignment* assignment = node.Find(key);
    if (assignment == nullptr || assignment->First() == nullptr) return {};
    return assignment->First()->kind == PiValue::Kind::String ? assignment->First()->AsString()
                                                               : assignment->First()->text;
}
} // namespace

PlatformDef ParsePlatformDef(const PiBlock& node) {
    PlatformDef platform;
    if (const PiAssignment* assignment = node.Find("TrackNum"); assignment != nullptr && assignment->First() != nullptr)
        platform.trackNum = assignment->First()->AsInt(1);
    platform.stoppable = IsYes(node.Find("Stoppable"), 0, true);
    const PiAssignment* openDoor = node.Find("OpenDoor");
    platform.openDoor[0] = IsYes(openDoor, 0, false);
    platform.openDoor[1] = IsYes(openDoor, 1, false);
    platform.railPlugin = StringAssignment(node, "RailPlugin");
    platform.tiePlugin = StringAssignment(node, "TiePlugin");
    platform.girderPlugin = StringAssignment(node, "GirderPlugin");
    platform.pierPlugin = StringAssignment(node, "PierPlugin");
    platform.linePlugin = StringAssignment(node, "LinePlugin");
    platform.polePlugin = StringAssignment(node, "PolePlugin");
    platform.liftRailSurface = IsYes(node.Find("LiftRailSurface"), 0, true);
    platform.enableCant = IsYes(node.Find("EnableCant"), 0, true);
    platform.parentObject = StringAssignment(node, "ParentObject");
    for (const PiAssignment& assignment : node.assignments) {
        if (assignment.key != "Coord" || assignment.First() == nullptr) continue;
        rs::core::Vec3 coord;
        assignment.First()->AsVec3(coord.x, coord.y, coord.z);
        platform.coords.push_back(coord);
    }
    return platform;
}
} // namespace rs::legacy_pi::schema_detail

namespace rs::legacy_pi {
int FindPlatformParentPart(const std::vector<ModelPart>& parts, const std::string& parentObject) {
    if (parentObject.empty()) return -1;
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].label == parentObject) return static_cast<int>(i);
    return -1;
}
} // namespace rs::legacy_pi
