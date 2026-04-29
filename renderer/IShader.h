#pragma once
#include <string>

#include "math/Mat3.h"
#include "math/Mat4.h"
#include "math/Vec3.h"
#include "math/Vec4.h"

namespace Horo {

class IShader {
public:
    virtual ~IShader() = default;

    virtual void Bind()   const = 0;
    virtual void Unbind() const = 0;

    virtual void SetInt(const std::string& name, int value)         = 0;
    virtual void SetFloat(const std::string& name, float value)     = 0;
    virtual void SetVec2(const std::string& name, float x, float y) = 0;
    virtual void SetVec3(const std::string& name, const Vec3& v)    = 0;
    virtual void SetVec4(const std::string& name, const Vec4& v)    = 0;
    virtual void SetMat3(const std::string& name, const Mat3& m)    = 0;
    virtual void SetMat4(const std::string& name, const Mat4& m)    = 0;

    // Upload count matrices to uniform `name[0]` through `name[count-1]`.
    virtual void SetMat4Array(const std::string& name, int count, const float* data) = 0;

    virtual bool IsValid() const = 0;
};

} // namespace Horo
