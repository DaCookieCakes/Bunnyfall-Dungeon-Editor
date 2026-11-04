#include "edit/transform.h"

#include "catalog/space.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace de::edit {

std::int32_t modelYRotationFor(std::int32_t turn) {
    // Which way Daggerfall's yRotation turns in editor space, read off the rotation itself: a quarter turn of +512
    // takes the model's +Z (north) to +X (east) when the sign is +1.
    static const int sign = (catalog::rdbModelRotation(0, 512, 0) * catalog::Vec3{0, 0, 1}).x > 0 ? 1 : -1;
    return sign * turn;
}

formats::RdbObject transformed(const formats::RdbObject& start, std::array<std::int32_t, 3> offset, std::int32_t turn,
                               double pivotX, double pivotZ) {
    auto obj = start;
    if (turn != 0) {
        const double a = turn * 2 * std::numbers::pi / catalog::kRotationUnitsPerTurn;
        const double ox = start.x - pivotX, oz = start.z - pivotZ;
        // Clockwise from above in a left-handed, Y-up space: +Z goes to +X.
        obj.x = static_cast<std::int32_t>(std::lround(pivotX + ox * std::cos(a) + oz * std::sin(a)));
        obj.z = static_cast<std::int32_t>(std::lround(pivotZ - ox * std::sin(a) + oz * std::cos(a)));
        if (auto* m = std::get_if<formats::RdbModel>(&obj.resource)) {
            constexpr auto kTurn = catalog::kRotationUnitsPerTurn;
            m->yRotation = ((m->yRotation + modelYRotationFor(turn)) % kTurn + kTurn) % kTurn;   // 0..2047
        }
    }
    obj.x += offset[0];
    obj.y += offset[1];
    obj.z += offset[2];
    return obj;
}

void forgetRemoved(std::vector<ObjectSlot>& slots, ObjectSlot removed) {
    std::erase(slots, removed);
    for (auto& s : slots) {
        if (s.root == removed.root && s.object > removed.object) --s.object;
    }
}

} // namespace de::edit
