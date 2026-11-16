#include "SignalPanelViewModel.h"

#include <format>

#include "SignalBlockAction.h"
#include "../../world/alignment/GuidJson.h"

namespace rs::app {
std::vector<workspace::SignalPanelSignal> BuildSignalPanelSignals(const rs::world::TrackGraph& graph,
                                                                  const SignalBlockAction& action) {
    std::vector<workspace::SignalPanelSignal> result;
    result.reserve(graph.speedSignals.size());
    for (std::size_t index = 0; index < graph.speedSignals.size(); ++index) {
        const rs::world::SpeedSignal& source = graph.speedSignals[index];
        const SignalBlockSignalRowViewModel row = action.SignalRow(source);
        workspace::SignalPanelSignal signal;
        signal.id_ = rs::world::GuidToHex(source.guid);
        signal.name_ = std::format("信号{}", index + 1);
        signal.direction_ = source.direction >= 0 ? "forward" : "reverse";
        for (const SignalBlockStageViewModel& stage : row.stages) {
            signal.stages_.push_back({std::to_string(stage.speedKmh), !signal.stages_.empty()});
        }
        for (const int speedKmh : row.addableSpeeds) signal.addableSpeeds_.push_back(std::to_string(speedKmh));
        signal.aspectText_ = row.aspectText;
        signal.forcedRed_ = row.forcedRed;
        signal.edge_ = source.edge;
        signal.s_ = source.s;
        result.push_back(std::move(signal));
    }
    return result;
}

} // namespace rs::app
