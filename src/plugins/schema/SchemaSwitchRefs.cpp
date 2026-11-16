//
// SchemaSwitchRefs.cpp - トリガーやパーツが指定スイッチを参照しているかの判定（SchemaWalk.cpp から分離）
//
// 走査で集めた TriggerInfo を読むだけで、パーツ走査そのものには依存しない。
//
#include "SchemaInternal.h"

namespace rs::legacy_pi {

bool TriggerReferencesAnySwitch(const TriggerInfo& trig, const std::vector<std::string>& names) {
    for (const TriggerStep& step : trig.chain) {
        for (const std::string& name : names) {
            if (step.trigger == TriggerKind::Condition) {
                if (ExprReferencesSwitch(step.condition, name)) return true;
            } else if (step.switchIsExpression) {
                if (ExprReferencesSwitch(step.switchName, name)) return true;
            } else {
                if (step.switchName == name) return true;
            }
        }
    }
    return false;
}

void AddReferencedSwitches(const TriggerInfo& trig, const std::vector<std::string>& candidates,
                           std::set<std::string>& referenced) {
    for (const std::string& name : candidates) {
        if (referenced.count(name) != 0) continue;
        if (TriggerReferencesAnySwitch(trig, std::vector<std::string>{name})) referenced.insert(name);
    }
}

namespace {

bool PartReferencesAnySwitch(const ModelPart& part, const std::vector<std::string>& names) {
    for (const ChangeModelRef& r : part.changeModels) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const ChangeMaterialCustomizer& r : part.changeMaterials) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const TextureStep& step : part.textureSteps) {
        if (TriggerReferencesAnySwitch(step.Trig(), names)) return true;
    }
    for (const ChangeAlphaCustomizer& r : part.changeAlphas) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.envMaps) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.bumpMaps) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.alphaZeroTests) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.transparents) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.noCastShadows) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const MaterialFlagCustomizer& r : part.noReceiveShadows) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    for (const StaticCustomizer& r : part.customizers) {
        if (TriggerReferencesAnySwitch(r.trig, names)) return true;
    }
    return false;
}

} // namespace

bool DefinitionReferencesAnySwitch(const PluginDefinition& def, const std::vector<std::string>& names) {
    for (const ModelPart& part : def.parts) {
        if (PartReferencesAnySwitch(part, names)) return true;
    }
    for (const SoundEffectRef& sound : def.soundEffects) {
        if (TriggerReferencesAnySwitch(sound.trig, names)) return true;
    }
    for (const HeadlightDef& headlight : def.headlights) {
        if (TriggerReferencesAnySwitch(headlight.trig, names)) return true;
    }
    for (const ParticleDef& particle : def.particles) {
        if (TriggerReferencesAnySwitch(particle.trig, names)) return true;
    }
    return false;
}

} // namespace rs::legacy_pi
