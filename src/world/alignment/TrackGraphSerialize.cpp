#include "TrackGraphSerialize.h"

#include <array>

#include "Alignment.h"
#include "AlignmentEnumJson.h"
#include "GuidJson.h"
#include "JsonReadValue.h"

namespace rs::world {
namespace {

constexpr const char* kLogTag = "track_graph_serialize";

nlohmann::json TimeToJson(const DiaTimeOfDay& time) {
    return { { "hour", time.hour }, { "minute", time.minute }, { "second", time.second } };
}

DiaTimeOfDay TimeFromJson(const nlohmann::json& json) {
    DiaTimeOfDay result;
    if (!json.is_object()) return result;
    result.hour = JsonReadValue(json, "hour", result.hour, kLogTag);
    result.minute = JsonReadValue(json, "minute", result.minute, kLogTag);
    result.second = JsonReadValue(json, "second", result.second, kLogTag);
    return result;
}

nlohmann::json ConditionToJson(const PointCondition& condition) {
    const char* kind = condition.kind == PointCondition::Kind::BlockOccupancy ? "BlockOccupancy" : "TimeRange";
    nlohmann::json blocks = nlohmann::json::array();
    for (const Guid& guid : condition.blocks) blocks.push_back(GuidToJson(guid));
    return { { "enabled", condition.enabled }, { "kind", kind }, { "startTime", TimeToJson(condition.startTime) },
             { "endTime", TimeToJson(condition.endTime) }, { "blocks", blocks },
             { "requireAllOccupied", condition.requireAllOccupied }, { "hand", condition.hand } };
}

PointCondition ConditionFromJson(const nlohmann::json& json) {
    PointCondition result;
    if (!json.is_object()) return result;
    result.enabled = JsonReadValue(json, "enabled", result.enabled, kLogTag);
    result.kind = JsonReadValue(json, "kind", std::string("TimeRange"), kLogTag) == "BlockOccupancy"
                      ? PointCondition::Kind::BlockOccupancy : PointCondition::Kind::TimeRange;
    if (json.contains("startTime")) result.startTime = TimeFromJson(json["startTime"]);
    if (json.contains("endTime")) result.endTime = TimeFromJson(json["endTime"]);
    result.requireAllOccupied = JsonReadValue(json, "requireAllOccupied", result.requireAllOccupied, kLogTag);
    result.hand = JsonReadValue(json, "hand", result.hand, kLogTag);
    if (json.contains("blocks") && json["blocks"].is_array()) {
        for (const nlohmann::json& block : json["blocks"]) result.blocks.push_back(GuidFromJson(block));
    }
    return result;
}

nlohmann::json RoutineToJson(const std::vector<PointRoutineElement>& routine) {
    nlohmann::json result = nlohmann::json::array();
    for (const PointRoutineElement& element : routine) {
        result.push_back({ { "enabled", element.enabled }, { "hand", element.hand }, { "random", element.random } });
    }
    return result;
}

std::vector<PointRoutineElement> RoutineFromJson(const nlohmann::json& json) {
    std::vector<PointRoutineElement> result;
    if (!json.is_array()) return result;
    for (const nlohmann::json& item : json) {
        if (!item.is_object()) continue;
        PointRoutineElement element;
        element.enabled = JsonReadValue(item, "enabled", element.enabled, kLogTag);
        element.hand = JsonReadValue(item, "hand", element.hand, kLogTag);
        element.random = JsonReadValue(item, "random", element.random, kLogTag);
        result.push_back(element);
    }
    return result;
}

template <class T, class ToJson>
nlohmann::json IntervalsToJson(const IntervalList<T>& intervals, const char* valueKey, ToJson toJson) {
    nlohmann::json result = nlohmann::json::array();
    for (const typename IntervalList<T>::Span& span : intervals.Spans()) {
        result.push_back({ { "sStart", span.sStart }, { valueKey, toJson(span.value) } });
    }
    return result;
}

template <class T, class FromJson>
IntervalList<T> IntervalsFromJson(const nlohmann::json& json, const char* valueKey, FromJson fromJson) {
    std::vector<typename IntervalList<T>::Span> spans;
    if (!json.is_array()) return IntervalList<T>();
    for (const nlohmann::json& item : json) {
        if (!item.is_object() || !item.contains(valueKey)) continue;
        typename IntervalList<T>::Span span;
        span.sStart = JsonReadValue(item, "sStart", 0.0, kLogTag);
        span.value = fromJson(item[valueKey]);
        spans.push_back(std::move(span));
    }
    return IntervalList<T>::FromSpans(std::move(spans));
}

}  // namespace

std::optional<int> EdgeEndAtNode(const TrackEdge& edge, const Guid& nodeGuid) {
    const bool atA = EdgeLinkNode(edge, 0) == nodeGuid;
    const bool atB = EdgeLinkNode(edge, 1) == nodeGuid;
    if (atA == atB) return std::nullopt;
    return atB ? 1 : 0;
}

nlohmann::json NodeToJson(const TrackNode& node, const TrackGraph* graph) {
    // links は面(side)ごとに振り分ける。面内の並び順が activeSlot の添字になるため、
    // 元の links の順序を面ごとに保ったまま詰める。
    std::array<nlohmann::json, 2> faceLinks{ nlohmann::json::array(), nlohmann::json::array() };
    for (const TrackNode::Link& link : node.links) {
        const size_t face = (link.side == 1) ? 1 : 0;
        nlohmann::json linkJson = { { "edge", GuidToJson(link.edge) } };
        // edge の接続側ノードから端が決まるリンクは、端を書かない(読込で求め直す)。
        const TrackEdge* edge = nullptr;
        if (graph != nullptr) {
            const auto found = graph->edges.find(link.edge);
            if (found != graph->edges.end()) edge = &found->second;
        }
        if (edge == nullptr || EdgeEndAtNode(*edge, node.guid) != link.end) {
            linkJson["edgeEnd"] = link.end == 0 ? "a" : "b";
        }
        faceLinks[face].push_back(std::move(linkJson));
    }
    const nlohmann::json routine = RoutineToJson(node.pointRoutine);
    nlohmann::json trainRoutines = nlohmann::json::array();
    for (const PointTrainRoutine& group : node.pointTrainRoutines) {
        trainRoutines.push_back({ { "trainId", GuidToJson(group.trainId) },
                                  { "routine", RoutineToJson(group.routine) } });
    }
    nlohmann::json formationRoutines = nlohmann::json::array();
    for (const PointFormationRoutine& group : node.pointFormationRoutines) {
        formationRoutines.push_back({ { "formation", GuidToJson(group.formationId) },
                                      { "routine", RoutineToJson(group.routine) } });
    }
    nlohmann::json tagRoutines = nlohmann::json::array();
    for (const PointTagRoutine& group : node.pointTagRoutines) {
        tagRoutines.push_back({ { "tagId", group.tagId },
                                { "routine", RoutineToJson(group.routine) } });
    }
    nlohmann::json conditions = nlohmann::json::array();
    for (const PointCondition& condition : node.pointConditions) conditions.push_back(ConditionToJson(condition));
    nlohmann::json trainConditions = nlohmann::json::array();
    for (const PointTrainCondition& group : node.pointTrainConditions) {
        nlohmann::json groupConditions = nlohmann::json::array();
        for (const PointCondition& condition : group.conditions) groupConditions.push_back(ConditionToJson(condition));
        trainConditions.push_back({ { "train", GuidToJson(group.trainId) }, { "conditions", groupConditions } });
    }
    nlohmann::json tagConditions = nlohmann::json::array();
    for (const PointTagCondition& group : node.pointTagConditions) {
        nlohmann::json groupConditions = nlohmann::json::array();
        for (const PointCondition& condition : group.conditions) groupConditions.push_back(ConditionToJson(condition));
        tagConditions.push_back({ { "tagId", group.tagId }, { "conditions", groupConditions } });
    }
    nlohmann::json formationConditions = nlohmann::json::array();
    for (const PointFormationCondition& group : node.pointFormationConditions) {
        nlohmann::json groupConditions = nlohmann::json::array();
        for (const PointCondition& condition : group.conditions) groupConditions.push_back(ConditionToJson(condition));
        formationConditions.push_back({ { "formation", GuidToJson(group.formationId) }, { "conditions", groupConditions } });
    }
    nlohmann::json result = { { "guid", GuidToJson(node.guid) }, { "pos", Vec3ToJson(node.pos) }, { "tangent", Vec3ToJson(node.tangent) },
             { "up", Vec3ToJson(node.up) }, { "cant", node.cant },
             { "faces", {{{ "activeSlot", node.activeSlot[0] }, { "links", faceLinks[0] }}, {{ "activeSlot", node.activeSlot[1] }, { "links", faceLinks[1] }}} }, { "pointRoutine", routine },
             { "pointTrainRoutines", trainRoutines }, { "pointFormationRoutines", formationRoutines },
             { "pointTagRoutines", tagRoutines },
             { "pointConditions", conditions }, { "pointTrainConditions", trainConditions },
             { "pointTagConditions", tagConditions } };
    if (!formationConditions.empty()) result["pointFormationConditions"] = std::move(formationConditions);
    if (!node.pointName.empty()) result["pointName"] = node.pointName;
    if (!GuidIsNil(node.pointStationId)) result["pointStationId"] = GuidToJson(node.pointStationId);
    return result;
}

TrackNode NodeFromJson(const nlohmann::json& json, std::vector<size_t>* linksWithoutEnd) {
    TrackNode result;
    if (!json.is_object()) return result;
    if (json.contains("guid")) result.guid = GuidFromJson(json["guid"]);
    if (json.contains("pos")) result.pos = Vec3FromJson(json["pos"], {}, kLogTag);
    if (json.contains("tangent")) result.tangent = Vec3FromJson(json["tangent"], result.tangent, kLogTag);
    if (json.contains("up")) result.up = Vec3FromJson(json["up"], result.up, kLogTag);
    result.cant = JsonReadValue(json, "cant", result.cant, kLogTag);
    if (json.contains("faces") && json["faces"].is_array()) {
        for (size_t faceIndex = 0; faceIndex < json["faces"].size() && faceIndex < 2; ++faceIndex) {
        const nlohmann::json& face = json["faces"][faceIndex];
        result.activeSlot[faceIndex] = JsonReadValue(face, "activeSlot", result.activeSlot[faceIndex], kLogTag);
        if (!face.contains("links") || !face["links"].is_array()) continue;
        for (const nlohmann::json& item : face["links"]) {
            if (!item.is_object()) continue;
            TrackNode::Link link;
            if (item.contains("edge")) link.edge = GuidFromJson(item["edge"]);
            link.end = JsonReadValue(item, "edgeEnd", std::string("a"), kLogTag) == "b" ? 1 : 0;
            link.side = static_cast<int>(faceIndex);
            if (linksWithoutEnd != nullptr && !item.contains("edgeEnd")) linksWithoutEnd->push_back(result.links.size());
            result.links.push_back(link);
        }}
    }
    if (json.contains("pointRoutine")) result.pointRoutine = RoutineFromJson(json["pointRoutine"]);
    if (json.contains("pointTrainRoutines") && json["pointTrainRoutines"].is_array()) {
        for (const nlohmann::json& item : json["pointTrainRoutines"]) {
            if (!item.is_object()) continue;
            PointTrainRoutine group;
            if (item.contains("trainId")) group.trainId = GuidFromJson(item["trainId"], group.trainId);
            if (item.contains("routine")) group.routine = RoutineFromJson(item["routine"]);
            result.pointTrainRoutines.push_back(std::move(group));
        }
    }
    if (json.contains("pointFormationRoutines") && json["pointFormationRoutines"].is_array()) {
        for (const nlohmann::json& item : json["pointFormationRoutines"]) {
            if (!item.is_object()) continue;
            PointFormationRoutine group;
            if (item.contains("formation")) group.formationId = GuidFromJson(item["formation"], group.formationId);
            if (item.contains("routine")) group.routine = RoutineFromJson(item["routine"]);
            result.pointFormationRoutines.push_back(std::move(group));
        }
    }
    if (json.contains("pointTagRoutines") && json["pointTagRoutines"].is_array()) {
        for (const nlohmann::json& item : json["pointTagRoutines"]) {
            if (!item.is_object()) continue;
            PointTagRoutine group;
            group.tagId = JsonReadValue(item, "tagId", group.tagId, kLogTag);
            if (item.contains("routine")) group.routine = RoutineFromJson(item["routine"]);
            result.pointTagRoutines.push_back(std::move(group));
        }
    }
    if (json.contains("pointConditions") && json["pointConditions"].is_array()) {
        for (const nlohmann::json& item : json["pointConditions"]) {
            result.pointConditions.push_back(ConditionFromJson(item));
        }
    }
    // 列車・編成・タグの条件は「キー＋条件配列」という同一形状のため、
    // 読込処理を1つのラムダへ集約する(kindで書き込み先だけ切り替える)。
    enum class GroupKind { Train, Formation, Tag };
    const auto readGroups = [](const nlohmann::json& groups, GroupKind kind, TrackNode& node) {
        if (!groups.is_array()) return;
        for (const nlohmann::json& item : groups) {
            if (!item.is_object()) continue;
            std::vector<PointCondition> conditions;
            if (item.contains("conditions") && item["conditions"].is_array()) {
                for (const nlohmann::json& condition : item["conditions"]) {
                    conditions.push_back(ConditionFromJson(condition));
                }
            }
            if (kind == GroupKind::Train) {
                PointTrainCondition group;
                if (item.contains("train")) {
                    group.trainId = GuidFromJson(item["train"], group.trainId);
                }
                group.conditions = std::move(conditions);
                node.pointTrainConditions.push_back(std::move(group));
            } else if (kind == GroupKind::Formation) {
                PointFormationCondition group;
                if (item.contains("formation")) {
                    group.formationId = GuidFromJson(item["formation"], group.formationId);
                }
                group.conditions = std::move(conditions);
                node.pointFormationConditions.push_back(std::move(group));
            } else {
                PointTagCondition group;
                group.tagId = JsonReadValue(item, "tagId", group.tagId, kLogTag);
                group.conditions = std::move(conditions);
                node.pointTagConditions.push_back(std::move(group));
            }
        }
    };
    if (json.contains("pointTrainConditions")) readGroups(json["pointTrainConditions"], GroupKind::Train, result);
    if (json.contains("pointFormationConditions")) readGroups(json["pointFormationConditions"], GroupKind::Formation, result);
    if (json.contains("pointTagConditions")) readGroups(json["pointTagConditions"], GroupKind::Tag, result);
    result.pointName = JsonReadValue(json, "pointName", std::string(), kLogTag);
    if (json.contains("pointStationId")) result.pointStationId = GuidFromJson(json["pointStationId"], result.pointStationId);
    return result;
}

nlohmann::json EdgeToJson(const TrackEdge& edge) {
    nlohmann::json result = {
        { "guid", GuidToJson(edge.guid) }, { "nodeA", GuidToJson(edge.nodeA) }, { "nodeB", GuidToJson(edge.nodeB) },
        { "pluginSections", IntervalsToJson(edge.pluginSections, "plugins",
              [](const PluginSet& value) { return nlohmann::json(value.plugins); }) },
        { "speedLimit", edge.speedLimitKmh < 0.0f ? nlohmann::json(nullptr) : nlohmann::json(edge.speedLimitKmh) },
        { "cantA", edge.cantA }, { "cantB", edge.cantB }, { "cantMid", edge.cantMid },
        { "cantTransitionRatio", edge.cantTransitionRatio },
        { "cantAuto", edge.cantAuto }, { "cantAutoMax", edge.cantAutoMax },
        { "cantTransitionShape", TransitionShapeToJson(edge.cantTransitionShape) },
        { "nodeATangentSign", edge.nodeATangentSign }, { "nodeBTangentSign", edge.nodeBTangentSign }, { "layer", GuidIsNil(edge.layer) ? nlohmann::json(nullptr) : GuidToJson(edge.layer) },
        { "centerlineOnly", edge.centerlineOnly }, { "detachedFromCenterline", edge.detachedFromCenterline }
    };
    if (IsParentDrivenEdge(edge)) {
        result["parentEntity"] = GuidToJson(edge.parentEntity);
        result["parentPart"] = edge.parentPart;
    }
    // 論理継ぎ目のある端だけ、接続側ノードを書く(無い端は nodeA/nodeB が接続を兼ねる)。
    if (!GuidIsNil(edge.linkNodes[0])) result["linkNodeA"] = GuidToJson(edge.linkNodes[0]);
    if (!GuidIsNil(edge.linkNodes[1])) result["linkNodeB"] = GuidToJson(edge.linkNodes[1]);
    if (!edge.cantPoints.empty()) {
        nlohmann::json points = nlohmann::json::array();
        for (const CantControlPoint& point : edge.cantPoints)
            points.push_back({ { "s", point.s }, { "cant", point.cant } });
        result["cantPoints"] = std::move(points);
    }
    // curve と spline は保存しない。読込時に両端ノードの姿勢から作り直す。
    if (edge.refit.available) {
        result["refit"] = { { "shape", TransitionShapeToJson(edge.refit.shape) }, { "transitionLength", edge.refit.transitionLength } };
    }
    if (edge.centerlineOnly) {
        result["trackCount"] = edge.trackCount;
        result["trackSpacing"] = edge.trackSpacing;
        result["trackEdges"] = nlohmann::json::array();
        for (const Guid& guid : edge.trackEdges) result["trackEdges"].push_back(GuidToJson(guid));
    }
    return result;
}

TrackEdge EdgeFromJson(const nlohmann::json& json) {
    TrackEdge result;
    if (!json.is_object()) return result;
    if (json.contains("guid")) result.guid = GuidFromJson(json["guid"]);
    if (json.contains("nodeA")) result.nodeA = GuidFromJson(json["nodeA"]);
    if (json.contains("nodeB")) result.nodeB = GuidFromJson(json["nodeB"]);
    if (json.contains("parentEntity") && json["parentEntity"].is_string()) {
        result.parentEntity = GuidFromJson(json["parentEntity"]);
        result.parentPart = JsonReadValue(json, "parentPart", result.parentPart, kLogTag);
    }
    if (json.contains("linkNodeA")) result.linkNodes[0] = GuidFromJson(json["linkNodeA"]);
    if (json.contains("linkNodeB")) result.linkNodes[1] = GuidFromJson(json["linkNodeB"]);
    if (json.contains("layer") && json["layer"].is_string()) {
        result.layer = GuidFromJson(json["layer"]);
    }
    result.cantA = JsonReadValue(json, "cantA", result.cantA, kLogTag);
    result.cantB = JsonReadValue(json, "cantB", result.cantB, kLogTag);
    result.cantMid = JsonReadValue(json, "cantMid", result.cantMid, kLogTag);
    result.cantTransitionRatio = JsonReadValue(json, "cantTransitionRatio", result.cantTransitionRatio, kLogTag);
    result.cantAuto = JsonReadValue(json, "cantAuto", result.cantAuto, kLogTag);
    result.cantAutoMax = JsonReadValue(json, "cantAutoMax", result.cantAutoMax, kLogTag);
    if (json.contains("cantPoints") && json["cantPoints"].is_array()) {
        for (const nlohmann::json& item : json["cantPoints"]) {
            if (!item.is_object()) continue;
            result.cantPoints.push_back({ JsonReadValue(item, "s", 0.0f, kLogTag),
                                          JsonReadValue(item, "cant", 0.0f, kLogTag) });
        }
    }
    if (json.contains("cantTransitionShape")) {
        result.cantTransitionShape = TransitionShapeFromJson(json["cantTransitionShape"]);
    }
    result.nodeATangentSign = JsonReadValue(json, "nodeATangentSign", result.nodeATangentSign, kLogTag);
    // 値が無い古いファイルは既定+1のまま。グラフ全体の読込(TrackGraphFromJson)が
    // spline とノードから導出し直す。
    result.nodeBTangentSign = JsonReadValue(json, "nodeBTangentSign", result.nodeBTangentSign, kLogTag);
    result.centerlineOnly = JsonReadValue(json, "centerlineOnly", result.centerlineOnly, kLogTag);
    result.detachedFromCenterline = JsonReadValue(json, "detachedFromCenterline", result.detachedFromCenterline, kLogTag);
    if (json.contains("pluginSections")) {
        result.pluginSections = IntervalsFromJson<PluginSet>(json["pluginSections"], "plugins", [](const nlohmann::json& value) {
            PluginSet set;
            if (value.is_array()) set.plugins = value.get<std::vector<std::string>>();
            return set;
        });
    }
    if (json.contains("speedLimit") && json["speedLimit"].is_number()) {
        result.speedLimitKmh = json["speedLimit"].get<float>();
    }
    if (json.contains("refit") && json["refit"].is_object()) {
        const nlohmann::json& refitJson = json["refit"];
        if (refitJson.contains("shape")) result.refit.shape = TransitionShapeFromJson(refitJson["shape"]);
        result.refit.transitionLength = JsonReadValue(refitJson, "transitionLength", result.refit.transitionLength, kLogTag);
        // refit があることが「後編集できる」ことを表す(偽なら refit ごと保存しない)。
        result.refit.available = true;
    }
    if (result.centerlineOnly) {
        result.trackCount = JsonReadValue(json, "trackCount", result.trackCount, kLogTag);
        result.trackSpacing = JsonReadValue(json, "trackSpacing", result.trackSpacing, kLogTag);
        if (json.contains("trackEdges") && json["trackEdges"].is_array()) {
            for (const nlohmann::json& item : json["trackEdges"]) result.trackEdges.push_back(GuidFromJson(item));
        }
    }
    return result;
}

}  // namespace rs::world
