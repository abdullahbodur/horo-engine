#include "renderer/IVertexBuffer.h"

namespace Horo {

uint32_t ShaderDataTypeSize(ShaderDataType type) {
    using enum ShaderDataType;
    switch (type) {
        case Float:  return 4;
        case Float2: return 4 * 2;
        case Float3: return 4 * 3;
        case Float4: return 4 * 4;
        case Mat3:   return 4 * 3 * 3;
        case Mat4:   return 4 * 4 * 4;
        case Int:    return 4;
        case Int2:   return 4 * 2;
        case Int3:   return 4 * 3;
        case Int4:   return 4 * 4;
        case Bool:   return 1;
        case None:   return 0;
    }
    return 0;
}

BufferElement::BufferElement(ShaderDataType t, const std::string& n, bool norm)
    : name(n), type(t), size(ShaderDataTypeSize(t)), normalized(norm) {}

uint32_t BufferElement::GetComponentCount() const {
    using enum ShaderDataType;
    switch (type) {
        case Float:  return 1;
        case Float2: return 2;
        case Float3: return 3;
        case Float4: return 4;
        case Mat3:   return 9;
        case Mat4:   return 16;
        case Int:    return 1;
        case Int2:   return 2;
        case Int3:   return 3;
        case Int4:   return 4;
        case Bool:   return 1;
        case None:   return 0;
    }
    return 0;
}

BufferLayout::BufferLayout(std::initializer_list<BufferElement> elements)
    : m_Elements(elements) {
    CalculateOffsetsAndStride();
}

void BufferLayout::CalculateOffsetsAndStride() {
    size_t offset = 0;
    m_Stride = 0;
    for (auto& element : m_Elements) {
        element.offset  = offset;
        offset          += element.size;
        m_Stride        += element.size;
    }
}

} // namespace Horo
