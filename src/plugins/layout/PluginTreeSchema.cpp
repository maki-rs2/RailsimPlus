#include "PluginTreeSchema.h"

#include "../../platform/text/AsciiCase.h"
#include "../schema/parser/Parser.h"

namespace rs::legacy_layout {

namespace {

using rs::legacy_pi::PiBlock;
using rs::legacy_pi::PiValue;

constexpr std::string_view kTreeSuffix = "PluginTree";

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    return rs::platform_text::ToAsciiLower(std::string(a)) ==
           rs::platform_text::ToAsciiLower(std::string(b));
}

void ReadFileLine(const rs::legacy_pi::PiAssignment& a, PluginTreeDir& dir,
                  std::vector<std::string>& warnings) {
    const bool valid = a.values.size() >= 2 && a.values[0].kind == PiValue::Kind::Ident &&
                       a.values[1].kind == PiValue::Kind::String;
    if (!valid) {
        warnings.push_back("PluginTree: File 行の形式が不正です(種別, \"名前\" が必要)");
        return;
    }
    dir.files.push_back({a.values[0].text, a.values[1].text});
}

PluginTreeDir ReadDir(const PiBlock& block, std::string name,
                      std::vector<std::string>& warnings) {
    PluginTreeDir dir;
    dir.name = std::move(name);
    for (const rs::legacy_pi::PiAssignment& a : block.assignments)
        if (a.key == "File") ReadFileLine(a, dir, warnings);
    for (const PiBlock& child : block.blocks)
        if (child.name == "Directory") dir.dirs.push_back(ReadDir(child, child.label, warnings));
    return dir;
}

} // namespace

const PluginTreeDir* PluginTreeSet::Find(std::string_view kind) const {
    for (const auto& [key, dir] : trees)
        if (EqualsIgnoreCase(key, kind)) return &dir;
    return nullptr;
}

PluginTreeSet ParsePluginTrees(const rs::legacy_pi::PiDocument& doc,
                               std::vector<std::string>& warnings) {
    PluginTreeSet out;
    for (const PiBlock& node : doc.nodes) {
        const std::string_view name = node.name;
        if (name.size() <= kTreeSuffix.size() || !name.ends_with(kTreeSuffix)) continue;
        std::string key(name.substr(0, name.size() - kTreeSuffix.size()));
        out.trees.emplace_back(std::move(key), ReadDir(node, std::string(), warnings));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> ParseSelectedPlugins(const rs::legacy_pi::PiDocument& doc) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const PiBlock& node : doc.nodes) {
        if (node.name != "ConfigMode") continue;
        for (const PiBlock& child : node.blocks) {
            if (child.name != "SelectedPlugin") continue;
            for (const rs::legacy_pi::PiAssignment& a : child.assignments) {
                if (a.values.size() == 1 && a.values[0].kind == PiValue::Kind::String)
                    out.emplace_back(a.key, a.values[0].text);
            }
        }
    }
    return out;
}

std::optional<std::vector<std::pair<std::string, std::string>>> ParseSelectedPluginFile(
    const std::filesystem::path& path) {
    rs::legacy_pi::Diagnostics diag;
    const rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(path.wstring(), diag);
    if (diag.HasErrors()) return std::nullopt;
    return ParseSelectedPlugins(doc);
}

std::optional<PluginTreeSet> ParsePluginTreeFile(const std::filesystem::path& path,
                                                 std::vector<std::string>& warnings) {
    rs::legacy_pi::Diagnostics diag;
    const rs::legacy_pi::PiDocument doc = rs::legacy_pi::ParseFile(path.wstring(), diag);
    if (diag.HasErrors()) return std::nullopt;
    return ParsePluginTrees(doc, warnings);
}

} // namespace rs::legacy_layout
