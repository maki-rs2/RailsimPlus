#include "RmlUiUpdatePhase.h"

namespace rs::app::rmlui {

namespace {
bool insideContextUpdate = false;
} // namespace

bool IsInsideContextUpdate() { return insideContextUpdate; }

ContextUpdateScope::ContextUpdateScope() { insideContextUpdate = true; }

ContextUpdateScope::~ContextUpdateScope() { insideContextUpdate = false; }

} // namespace rs::app::rmlui
