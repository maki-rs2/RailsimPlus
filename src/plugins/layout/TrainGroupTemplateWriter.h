//
// TrainGroupTemplateWriter.h - TrainGroupTemplateDef -> TrainGroupTemplate\*.txt(編成テンプレート)本文
//
// ParseTrainGroupTemplateの逆。各車に Reverse と、SwitchOptionがあれば SwitchNum とともに書く。
//
#pragma once

#include "TemplateWriteText.h"
#include "TrainGroupTemplateSchema.h"

namespace rs::legacy_layout {

TemplateWriteResult WriteTrainGroupTemplate(const TrainGroupTemplateDef& def);

}  // namespace rs::legacy_layout
