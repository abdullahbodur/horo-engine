#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Horo {

enum class ShaderDataType {
    None = 0,
    Float, Float2, Float3, Float4,
    Mat3, Mat4,
    Int, Int2, Int3, Int4,
    Bool
};

uint32_t ShaderDataTypeSize(ShaderDataType type);

struct BufferElement {
    std::string    name;
    ShaderDataType type       = ShaderDataType::None;
    uint32_t       size       = 0;
    size_t         offset     = 0;
    bool           normalized = false;

    BufferElement() = default;
    BufferElement(ShaderDataType type, const std::string& name, bool normalized = false);

    uint32_t GetComponentCount() const;
};

class BufferLayout {
public:
    BufferLayout() = default;
    BufferLayout(std::initializer_list<BufferElement> elements);

    const std::vector<BufferElement>& GetElements() const { return m_Elements; }
    uint32_t GetStride() const { return m_Stride; }

    std::vector<BufferElement>::iterator       begin()       { return m_Elements.begin(); }
    std::vector<BufferElement>::iterator       end()         { return m_Elements.end(); }
    std::vector<BufferElement>::const_iterator begin() const { return m_Elements.begin(); }
    std::vector<BufferElement>::const_iterator end()   const { return m_Elements.end(); }

private:
    void CalculateOffsetsAndStride();
    std::vector<BufferElement> m_Elements;
    uint32_t m_Stride = 0;
};

class IVertexBuffer {
public:
    virtual ~IVertexBuffer() = default;

    virtual void Bind()   const = 0;
    virtual void Unbind() const = 0;

    virtual void SetData(const void* data, uint32_t size)  = 0; // NOSONAR
    virtual const BufferLayout& GetLayout()                const = 0;
    virtual void SetLayout(const BufferLayout& layout)     = 0;
};

} // namespace Horo
