// EnvFlag.h - 環境変数で切り替える診断・実験用フラグの読み方
#pragma once

#include <optional>

namespace rs::render {

// 環境変数の値(未設定は nullptr)の解釈。未設定・空は defaultValue、"0" は偽、それ以外は真。
bool ParseEnvFlag(const char* envValue, bool defaultValue = false);

// 環境変数 name を、起動後の最初の1回だけ読む用途の読み取り(呼び出し側が static に保持する)。
bool ReadEnvFlag(const char* name, bool defaultValue = false);

// 環境変数 name の数値(未設定は値なし)。開発用の上書きに使う。呼び出し側が static に保持する。
std::optional<float> ReadEnvFloat(const char* name);

// 全材質PBR統一モード(環境変数 RS_PBR_UNIFIED)。IBL有効のときだけ、Blinn-Phongの材質も含めてGGXとIBLで描く。
// 既定は有効で、"0" で Blinn-Phong の材質を分ける描き方になる。起動後に1回だけ環境変数を読む。
bool PbrUnifiedEnabled();

} // namespace rs::render
