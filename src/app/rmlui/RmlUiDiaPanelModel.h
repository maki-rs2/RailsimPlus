#pragma once

#include <RmlUi/Core/DataModelHandle.h>

namespace rs::app::rmlui {

// ダイヤの左タブの表示値(DiaPanelPresentation)の型を、RmlUiのデータモデルへ登録する。
void RegisterDiaPanelModel(Rml::DataModelConstructor& constructor);

} // namespace rs::app::rmlui
