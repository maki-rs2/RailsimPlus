//
// RmlUiUpdateVariable.h - データモデルの変数を、値が変わったときだけ書き換えてDirtyにする小さな共通処理
//
#pragma once

#include <RmlUi/Core/DataModelHandle.h>

namespace rs::app::rmlui {

// slotがvalueと違うときだけ書き換え、その変数をDirtyにする(変わっていない変数は触らない)。
// nameにはトップレベルの変数名だけを渡す(構造体のメンバを"xxx.yyy"と書いても、RmlUiは通知を受け付けない)。
template <typename T>
void UpdateVariable(Rml::DataModelHandle& handle, const char* name, T& slot, const T& value) {
    if (slot == value) return;
    slot = value;
    handle.DirtyVariable(name);
}

} // namespace rs::app::rmlui
