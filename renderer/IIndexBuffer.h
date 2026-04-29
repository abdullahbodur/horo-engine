#pragma once
#include <cstdint>

namespace Horo {

class IIndexBuffer {
public:
    virtual ~IIndexBuffer() = default;

    virtual void Bind()   const = 0;
    virtual void Unbind() const = 0;

    virtual uint32_t GetCount() const = 0;
};

} // namespace Horo
