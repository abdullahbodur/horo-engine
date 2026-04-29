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

    void SetInt(const std::string &, int)               override {}
    void SetFloat(const std::string &, float)           override {}
    void SetVec2(const std::string &, float, float)     override {}
    void SetVec3(const std::string &, const Vec3 &)     override {}
    void SetVec4(const std::string &, const Vec4 &)     override {}
    void SetMat3(const std::string &, const Mat3 &)     override {}
    void SetMat4(const std::string &, const Mat4 &)     override {}
    void SetMat4Array(const std::string &, int, const float *) override {}

    bool IsValid() const override { return true; }
};

} // namespace Horo
