#include "Rs2DiaSchema.h"

#include "PiValueAccessors.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiAssignment;
using rs::legacy_pi::PiBlock;

bool ParseUseDefault(const PiBlock& block) {
    const PiAssignment* assignment = block.Find("UseDefault");
    return !assignment || assignment->values.empty() ||
           !assignment->values[0].IsNo();
}

} // namespace

Rs2DiaElement ParseDiaElementBlock(const PiBlock& block) {
    Rs2DiaElement element;
    element.present = true;
    element.action = GetInt(block, "Action", 0);
    element.timeType = GetInt(block, "TimeType", 0);
    const PiAssignment* time = block.Find("Time");
    if (time && time->values.size() >= 3) {
        element.time[0] = time->values[0].AsInt(0);
        element.time[1] = time->values[1].AsInt(0);
        element.time[2] = time->values[2].AsInt(0);
    }
    element.offset = GetDouble(block, "Offset", 0.0);
    return element;
}

Rs2PointInst ParsePointInst(const PiBlock& conn) {
    Rs2PointInst pointInst;
    const PiBlock* pointInstBlock = conn.FirstChild("PointInst");
    if (!pointInstBlock) return pointInst;

    pointInst.present = true;
    pointInst.name = GetString(*pointInstBlock, "Name");
    for (const PiBlock* pointList : pointInstBlock->Children("PointList")) {
        Rs2PointRoutine routine;
        routine.trainGroup = GetAddress(*pointList, "TrainGroup");
        routine.useDefault = ParseUseDefault(*pointList);
        const std::vector<const PiAssignment*> points = FindAllAssignments(*pointList, "Point");
        routine.points.reserve(points.size());
        for (const PiAssignment* point : points) {
            if (!point->values.empty()) routine.points.push_back(point->values[0].AsInt(0));
        }
        pointInst.routines.push_back(std::move(routine));
    }
    return pointInst;
}

Rs2DiaInst ParseDiaInst(const PiBlock& station) {
    Rs2DiaInst diaInst;
    const PiBlock* diaInstBlock = station.FirstChild("DiaInst");
    if (!diaInstBlock) return diaInst;

    diaInst.present = true;
    diaInst.name = GetString(*diaInstBlock, "Name");
    for (const PiBlock* diaList : diaInstBlock->Children("DiaList")) {
        Rs2DiaRoutine routine;
        routine.trainGroup = GetAddress(*diaList, "TrainGroup");
        routine.useDefault = ParseUseDefault(*diaList);
        const std::vector<const PiBlock*> elements = diaList->Children("DiaElement");
        routine.elements.reserve(elements.size());
        for (const PiBlock* element : elements) {
            routine.elements.push_back(ParseDiaElementBlock(*element));
        }
        diaInst.routines.push_back(std::move(routine));
    }
    return diaInst;
}

} // namespace rs::legacy_layout
