//
// SceneryTemplateWriter.h - SceneryTemplateDef -> StationBuildTemplate\*.txt / StructBuildTemplate\*.txt 本文
//
// ParseSceneryTemplateの逆。各個体に Pos・Dir・Up と、SwitchOptionがあれば SwitchNum とともに書く。
//
#pragma once

#include "SceneryTemplateSchema.h"
#include "TemplateWriteText.h"

namespace rs::legacy_layout {

TemplateWriteResult WriteSceneryTemplate(const SceneryTemplateDef& def);

}  // namespace rs::legacy_layout
