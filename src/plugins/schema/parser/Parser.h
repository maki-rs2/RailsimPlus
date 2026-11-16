//
// Parser.h - PI/.rs2 共通ブロック構文パーサ
//
#pragma once

#include <string>

#include "Ast.h"
#include "Diagnostics.h"

namespace rs::legacy_pi {

// UTF-8 文字列（CP932->UTF-8 済み）を AST 化する。
// 未知トークンは可能な限りスキップして継続する（fail-fast しない）。
PiDocument Parse(const std::string& utf8, Diagnostics& diag);

// ファイルを読み CP932->UTF-8 して Parse する。読込失敗時は空ドキュメント。
PiDocument ParseFile(const std::wstring& path, Diagnostics& diag);

} // namespace rs::legacy_pi
