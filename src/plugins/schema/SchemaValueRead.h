//
// SchemaValueRead.h - PIブロックから数値・色を1つ読む共通の小さな関数
//
// EnvSchema.cpp と LensFlareSchema.cpp が共用する。キーが無いときは何もせずfalse、
// キーはあるのに型が違うときは診断へ警告を出してfalseを返す。
//
#pragma once

#include "PiColor.h"
#include "parser/Ast.h"
#include "parser/Diagnostics.h"

namespace rs::legacy_pi {

// キーが数値で書かれているときだけvalueへ入れてtrueを返す。
bool ReadNumber(const PiBlock& block, const char* key, Diagnostics& diag, double& value);

// キーが色(#AARRGGBB)で書かれているときだけcolorへ入れてtrueを返す。
bool ReadColor(const PiBlock& block, const char* key, Diagnostics& diag, PiColor& color);

} // namespace rs::legacy_pi
