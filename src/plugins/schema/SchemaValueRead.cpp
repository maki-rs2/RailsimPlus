#include "SchemaValueRead.h"

#include <string>

#include "SchemaInternal.h"

namespace rs::legacy_pi {

bool ReadNumber(const PiBlock& block, const char* key, Diagnostics& diag, double& value) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr) return false;
    const PiValue* first = assignment->First();
    if (first == nullptr || first->kind != PiValue::Kind::Number) {
        diag.Warn(0, 0, std::string("[pi] ") + block.name + "." + key + " is not a number");
        return false;
    }
    value = first->number;
    return true;
}

bool ReadColor(const PiBlock& block, const char* key, Diagnostics& diag, PiColor& color) {
    const PiAssignment* assignment = block.Find(key);
    if (assignment == nullptr) return false;
    const PiValue* first = assignment->First();
    if (first == nullptr || first->kind != PiValue::Kind::HexColor ||
        !ParseArgbHex(first->text, color.r, color.g, color.b, color.a)) {
        diag.Warn(0, 0, std::string("[pi] ") + block.name + "." + key + " is not a #AARRGGBB color");
        return false;
    }
    return true;
}

} // namespace rs::legacy_pi
