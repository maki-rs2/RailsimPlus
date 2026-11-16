#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../StretchZyMeshBuilder.h"

namespace rs::legacy_plugin {

struct LoadedPlugin;

// partWorldsはpartsと同じ並び。第1取り付け先のローカル座標で生成する。
// 取り付け先を解決できなければfalseを返し、出力は変更しない。
// systemWorldはシステム名の取り付け先に共通の、オブジェクト自身の基準行列。
bool BuildStretchZyLocalMesh(const rs::legacy_pi::StretchZyDef& stretch,
    const std::vector<rs::legacy_pi::ModelPart>& parts,
    const std::vector<rs::core::Mat4>& partWorlds, StretchZyMeshResult& out,
    int* anchorPartDefIndex = nullptr,
    const rs::core::Mat4& systemWorld = rs::core::Mat4::Identity());

struct StretchZyVertexRange {
    size_t first = 0;
    size_t count = 0;
};

// 該当頂点がない、または連続していない場合は空の範囲を返す。
StretchZyVertexRange FindStretchZyVertexRange(const rs::render::Mesh& assembly,
                                            uint32_t placementIndex);

// RebuildAssemblyが末尾に積んだ配置からstretchZyPartsの添字を返す。対応しなければ-1。
int FindStretchZyPartIndex(const LoadedPlugin& plugin, uint32_t placementIndex);

// 生成済みの全StretchZYが配置順に並ぶ末尾だけを調べる。
StretchZyVertexRange FindStretchZyTailRange(const LoadedPlugin& plugin);

// 末尾の頂点をコピーし、解決できる配置の位置と法線だけを更新する。
std::vector<rs::render::MeshVertex> BuildStretchZyDynamicVertices(
    const LoadedPlugin& plugin, const std::vector<rs::core::Mat4>& partWorlds,
    const rs::core::Mat4& systemWorld = rs::core::Mat4::Identity());

// 頂点数や範囲が合わなければ何も変更しない。AABBの更新は呼び出し側で行う。
bool UpdateStretchZyVertices(rs::render::Mesh& mesh, StretchZyVertexRange range,
                            const StretchZyMeshResult& geometry);

} // namespace rs::legacy_plugin
