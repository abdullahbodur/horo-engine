#include "math/Vec2.h"

#include <sstream>

#include "math/MathUtils.h"

namespace Horo {
    Vec2 Vec2::Normalized() const {
        float len = Length();
        if (NearlyZero(len))
            return Vec2::Zero();
        return *this / len;
    }

    std::string Vec2::ToString() const {
        std::ostringstream ss;
        ss << "Vec2(" << x << ", " << y << ")";
        return ss.str();
    }
} // namespace Horo
