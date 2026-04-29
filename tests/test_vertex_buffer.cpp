// test_vertex_buffer.cpp
//
// Unit tests for renderer/IVertexBuffer.cpp — pure CPU logic, no GPU required.
// Covers: ShaderDataTypeSize, BufferElement, BufferLayout.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>

#include "renderer/IVertexBuffer.h"

using namespace Horo;

// ===========================================================================
// ShaderDataTypeSize
// ===========================================================================

TEST_CASE("ShaderDataTypeSize: None returns 0", "[renderer][vertex-buffer]") {
    CHECK(ShaderDataTypeSize(ShaderDataType::None) == 0u);
}

TEST_CASE("ShaderDataTypeSize: Float scalar and vector types", "[renderer][vertex-buffer]") {
    CHECK(ShaderDataTypeSize(ShaderDataType::Float)  ==  4u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Float2) ==  8u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Float3) == 12u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Float4) == 16u);
}

TEST_CASE("ShaderDataTypeSize: Mat types", "[renderer][vertex-buffer]") {
    CHECK(ShaderDataTypeSize(ShaderDataType::Mat3) == 36u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Mat4) == 64u);
}

TEST_CASE("ShaderDataTypeSize: Int scalar and vector types", "[renderer][vertex-buffer]") {
    CHECK(ShaderDataTypeSize(ShaderDataType::Int)  ==  4u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Int2) ==  8u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Int3) == 12u);
    CHECK(ShaderDataTypeSize(ShaderDataType::Int4) == 16u);
}

TEST_CASE("ShaderDataTypeSize: Bool returns 1", "[renderer][vertex-buffer]") {
    CHECK(ShaderDataTypeSize(ShaderDataType::Bool) == 1u);
}

// ===========================================================================
// BufferElement — construction and defaults
// ===========================================================================

TEST_CASE("BufferElement: default construction has None type and zero fields", "[renderer][vertex-buffer]") {
    BufferElement e;
    CHECK(e.type       == ShaderDataType::None);
    CHECK(e.size       == 0u);
    CHECK(e.offset     == 0u);
    CHECK(e.normalized == false);
    CHECK(e.name.empty());
}

TEST_CASE("BufferElement: constructor sets name, type and derives size", "[renderer][vertex-buffer]") {
    BufferElement e{ShaderDataType::Float3, "position"};
    CHECK(e.name       == "position");
    CHECK(e.type       == ShaderDataType::Float3);
    CHECK(e.size       == 12u);
    CHECK(e.offset     == 0u);   // set by BufferLayout, not constructor
    CHECK(e.normalized == false);
}

TEST_CASE("BufferElement: normalized flag is forwarded", "[renderer][vertex-buffer]") {
    BufferElement e{ShaderDataType::Float2, "uv", true};
    CHECK(e.normalized == true);
}

TEST_CASE("BufferElement: size matches ShaderDataTypeSize for all types", "[renderer][vertex-buffer]") {
    using SDT = ShaderDataType;
    const SDT types[] = {SDT::Float, SDT::Float2, SDT::Float3, SDT::Float4,
                         SDT::Mat3,  SDT::Mat4,
                         SDT::Int,   SDT::Int2,   SDT::Int3,   SDT::Int4,
                         SDT::Bool,  SDT::None};
    for (auto t : types) {
        BufferElement e{t, "x"};
        CHECK(e.size == ShaderDataTypeSize(t));
    }
}

// ===========================================================================
// BufferElement::GetComponentCount
// ===========================================================================

TEST_CASE("BufferElement::GetComponentCount: None returns 0", "[renderer][vertex-buffer]") {
    CHECK(BufferElement(ShaderDataType::None, "x").GetComponentCount() == 0u);
}

TEST_CASE("BufferElement::GetComponentCount: Float scalar and vectors", "[renderer][vertex-buffer]") {
    using SDT = ShaderDataType;
    CHECK(BufferElement(SDT::Float,  "x").GetComponentCount() == 1u);
    CHECK(BufferElement(SDT::Float2, "x").GetComponentCount() == 2u);
    CHECK(BufferElement(SDT::Float3, "x").GetComponentCount() == 3u);
    CHECK(BufferElement(SDT::Float4, "x").GetComponentCount() == 4u);
}

TEST_CASE("BufferElement::GetComponentCount: Mat types", "[renderer][vertex-buffer]") {
    using SDT = ShaderDataType;
    CHECK(BufferElement(SDT::Mat3, "x").GetComponentCount() ==  9u);
    CHECK(BufferElement(SDT::Mat4, "x").GetComponentCount() == 16u);
}

TEST_CASE("BufferElement::GetComponentCount: Int scalar and vectors", "[renderer][vertex-buffer]") {
    using SDT = ShaderDataType;
    CHECK(BufferElement(SDT::Int,  "x").GetComponentCount() == 1u);
    CHECK(BufferElement(SDT::Int2, "x").GetComponentCount() == 2u);
    CHECK(BufferElement(SDT::Int3, "x").GetComponentCount() == 3u);
    CHECK(BufferElement(SDT::Int4, "x").GetComponentCount() == 4u);
}

TEST_CASE("BufferElement::GetComponentCount: Bool returns 1", "[renderer][vertex-buffer]") {
    CHECK(BufferElement(ShaderDataType::Bool, "x").GetComponentCount() == 1u);
}

// ===========================================================================
// BufferLayout
// ===========================================================================

TEST_CASE("BufferLayout: default construction has zero stride and no elements", "[renderer][vertex-buffer]") {
    BufferLayout layout;
    CHECK(layout.GetStride() == 0u);
    CHECK(layout.GetElements().empty());
}

TEST_CASE("BufferLayout: single element — stride equals element size, offset is 0", "[renderer][vertex-buffer]") {
    BufferLayout layout = {{ShaderDataType::Float3, "position"}};
    CHECK(layout.GetStride() == 12u);
    REQUIRE(layout.GetElements().size() == 1u);
    CHECK(layout.GetElements()[0].offset == 0u);
    CHECK(layout.GetElements()[0].size   == 12u);
}

TEST_CASE("BufferLayout: multiple elements have packed offsets and correct stride", "[renderer][vertex-buffer]") {
    BufferLayout layout = {
        {ShaderDataType::Float3, "position"},   // 12 bytes, offset 0
        {ShaderDataType::Float2, "uv"},         //  8 bytes, offset 12
        {ShaderDataType::Float4, "color"},      // 16 bytes, offset 20
    };
    CHECK(layout.GetStride() == 36u);
    REQUIRE(layout.GetElements().size() == 3u);
    CHECK(layout.GetElements()[0].offset == 0u);
    CHECK(layout.GetElements()[1].offset == 12u);
    CHECK(layout.GetElements()[2].offset == 20u);
}

TEST_CASE("BufferLayout: dense integer layout offsets and stride", "[renderer][vertex-buffer]") {
    BufferLayout layout = {
        {ShaderDataType::Int,  "id"},       // 4 bytes, offset 0
        {ShaderDataType::Int2, "tile"},     // 8 bytes, offset 4
    };
    CHECK(layout.GetStride() == 12u);
    CHECK(layout.GetElements()[0].offset == 0u);
    CHECK(layout.GetElements()[1].offset == 4u);
}

TEST_CASE("BufferLayout: range-for const iteration visits all elements", "[renderer][vertex-buffer]") {
    const BufferLayout layout = {
        {ShaderDataType::Float3, "a"},
        {ShaderDataType::Float2, "b"},
        {ShaderDataType::Float,  "c"},
    };
    int count = 0;
    for (const auto& elem : layout) {
        (void)elem;
        ++count;
    }
    CHECK(count == 3);
}

TEST_CASE("BufferLayout: mutable iteration allows element mutation", "[renderer][vertex-buffer]") {
    BufferLayout layout = {{ShaderDataType::Float4, "color"}};
    for (auto& elem : layout)
        elem.name = "modified";
    CHECK(layout.GetElements()[0].name == "modified");
}

TEST_CASE("BufferLayout: Bool element in layout", "[renderer][vertex-buffer]") {
    BufferLayout layout = {
        {ShaderDataType::Float, "val"},    // 4 bytes, offset 0
        {ShaderDataType::Bool,  "flag"},   // 1 byte,  offset 4
    };
    CHECK(layout.GetStride() == 5u);
    CHECK(layout.GetElements()[1].offset == 4u);
}
