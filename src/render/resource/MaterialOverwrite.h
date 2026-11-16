//
// MaterialOverwrite.h - 材質の上書きファイル(.x と同名の .mat)の読込と適用
//
// .x を読み終えた直後に1回だけ、材質の色と画像の枠(色・法線・PBR)を上書きする。
// 書かなかった項目は上書きしない。画像の相対パスは .mat のあるフォルダが基準。
// パースは PI と共通のブロックパーサを使う。
//
#pragma once

#include <string>
#include <vector>

#include "../mesh/Mesh.h"

namespace rs::legacy_x {

// .x のパスに対応する .mat のパスを返す(拡張子を外して .Mat を付けたファイル。
// 大文字小文字は区別しない)。無ければ空文字列。
std::wstring FindMaterialOverwriteFile(const std::wstring& xPath);

// .mat の本文(UTF-8)を mesh の材質へ適用する。matDirUtf8 は画像パスの基準フォルダ。
// 文法の誤りは警告を log に積んで読み飛ばす。
void ApplyMaterialOverwriteText(const std::string& utf8, const std::string& matDirUtf8,
                                rs::render::Mesh& mesh, std::vector<std::string>& log);

// xPath に対応する .mat があれば読んで適用する。無ければ何もしない。
void ApplyMaterialOverwriteFor(const std::wstring& xPath, rs::render::Mesh& mesh,
                               std::vector<std::string>& log);

} // namespace rs::legacy_x
