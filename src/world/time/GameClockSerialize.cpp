#include "GameClockSerialize.h"

namespace rs::world {

nlohmann::json GameClockToJson(const GameClock& clock) {
    return {
        { "date", { clock.Year(), clock.Month(), clock.Day() } },
        { "secondsOfDay", clock.SecondsOfDay() },
        { "timeScale", clock.TimeScale() },
    };
}

void ApplyGameClockFromJson(const nlohmann::json& json, GameClock& clock) {
    if (!json.is_object() || !json.contains("date") || !json.at("date").is_array() || json.at("date").size() != 3 || !json.contains("secondsOfDay")) {
        return;
    }
    try {
        const DateParts date{ json.at("date")[0].get<int>(), json.at("date")[1].get<int>(), json.at("date")[2].get<int>() };
        const int64_t dayNumber = FromDateParts(date);
        const double secondsOfDay = json.at("secondsOfDay").get<double>();
        clock.SetDateTime(dayNumber, secondsOfDay);
        clock.SetTimeScale(json.value("timeScale", clock.TimeScale()));
    } catch (const nlohmann::json::exception&) {
    }
}

}  // namespace rs::world
