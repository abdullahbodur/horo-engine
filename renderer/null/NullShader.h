#pragma once
#include <string>

#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "math/Vec4.h"
#include "renderer/IShader.h"

namespace Horo {

class NullShader final : public IShader {
public:
    mutable int bindCount   = 0;
    mutable int unbindCount = 0;

    void Bind()   const override { ++bindCount; }
    void Unbind() const override { ++unbindCount; }

    void SetInt(const std::string &, int)               override { /* No-op: null renderer. */ }
    void SetFloat(const std::string &, float)           override { /* No-op: null renderer. */ }
    void SetVec2(const std::string &, float, float)     override { /* No-op: null renderer. */ }
    void SetVec3(const std::string &, const Vec3 &)     override { /* No-op: null renderer. */ }
    void SetVec4(const std::string &, const Vec4 &)     override { /* No-op: null renderer. */ }
    void SetMat3(const std::string &, const Mat3 &)     override { /* No-op: null renderer. */ }
    void SetMat4(const std::string &, const Mat4 &)     override { /* No-op: null renderer. */ }
    void SetMat4Array(const std::string &, int, const float *) override { /* No-op: null renderer. */ }

    bool IsValid() const override { return true; }
};

} // namespace Horo
