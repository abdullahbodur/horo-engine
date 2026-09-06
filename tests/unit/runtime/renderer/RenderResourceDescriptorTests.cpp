#include "Horo/Runtime/Render/RenderResourceDescriptorErrors.h"
#include "Horo/Runtime/Render/RenderResourceDescriptors.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {
    using namespace Horo;
    using namespace Horo::Render;

    constexpr RenderTextureHandle TextureHandle{{1}, 1, 1};

    [[nodiscard]] RenderTextureDescriptor ColorTexture(const FramebufferExtent extent = {2, 2}) {
        return {.extent = extent, .format = RenderTextureFormat::Rgba8Unorm, .usage = RenderTextureUsage::Sampled};
    }

    [[nodiscard]] RenderTextureViewDescriptor ColorView() {
        return {.texture = TextureHandle, .format = RenderTextureFormat::Rgba8Unorm, .aspect = RenderTextureAspect::Color};
    }

    void CheckError(const Result<void> &result, const ErrorCodeDescriptor &descriptor) {
        REQUIRE(result.HasError());
        CHECK(result.ErrorValue().code.Value() == descriptor.code.Value());
    }
}  // namespace

TEST_CASE("Existing buffer and texture descriptor values remain source compatible", "[runtime][renderer][resource-descriptor]") {
    STATIC_CHECK(static_cast<std::uint8_t>(RenderBufferUsage::Vertex) == 1U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderBufferUsage::Index) == 2U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderBufferUsage::CopySource) == 4U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderBufferUsage::CopyDestination) == 8U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureDimension::TwoD) == 0U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureFormat::Rgba8Unorm) == 0U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureFormat::Depth24Stencil8) == 1U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureFormat::Depth32Float) == 2U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureUsage::Sampled) == 1U);
    STATIC_CHECK(static_cast<std::uint8_t>(RenderTextureUsage::RenderAttachment) == 2U);

    constexpr RenderBufferDescriptor buffer{16, RenderBufferUsage::Vertex, RenderBufferAccess::DeviceLocal};
    constexpr RenderTextureDescriptor texture{RenderTextureDimension::TwoD, {8, 4}, RenderTextureFormat::Rgba8Unorm, 1, 1, 1,
                                              RenderTextureUsage::Sampled};
    STATIC_CHECK(buffer.IsValid());
    STATIC_CHECK(texture.IsValid());
}

TEST_CASE("Buffer descriptors reject malformed structure with typed errors", "[runtime][renderer][resource-descriptor]") {
    const RenderBufferDescriptor valid{.byteSize = 16,
                                       .usage = RenderBufferUsage::Vertex | RenderBufferUsage::CopyDestination,
                                       .access = RenderBufferAccess::DeviceLocal};
    CHECK(ValidateRenderBufferDescriptor(valid).HasValue());
    CHECK(ValidateRenderBufferDescriptor({.byteSize = 256,
                                          .usage = RenderBufferUsage::Uniform | RenderBufferUsage::Storage | RenderBufferUsage::Indirect,
                                          .access = RenderBufferAccess::HostVisible})
              .HasValue());

    CheckError(ValidateRenderBufferDescriptor({.usage = RenderBufferUsage::Vertex}), RenderResourceDescriptorErrors::BufferInvalid);
    CheckError(ValidateRenderBufferDescriptor({.byteSize = 16}), RenderResourceDescriptorErrors::BufferInvalid);
    CheckError(ValidateRenderBufferDescriptor({.byteSize = 16, .usage = static_cast<RenderBufferUsage>(0x80U)}),
               RenderResourceDescriptorErrors::BufferInvalid);
    CheckError(ValidateRenderBufferDescriptor(
                   {.byteSize = 16, .usage = RenderBufferUsage::Vertex, .access = static_cast<RenderBufferAccess>(0xFFU)}),
               RenderResourceDescriptorErrors::BufferInvalid);
}

TEST_CASE("Buffer initial data remains an explicit synchronously borrowed value", "[runtime][renderer][resource-descriptor]") {
    const RenderBufferDescriptor descriptor{.byteSize = 4, .usage = RenderBufferUsage::Vertex};
    std::array<std::byte, 4> bytes{};
    CHECK(ValidateRenderBufferInitialData(descriptor, {}).HasValue());
    CHECK(ValidateRenderBufferInitialData(descriptor, {bytes}).HasValue());
    CheckError(ValidateRenderBufferInitialData(descriptor, {std::span{bytes}.first<3>()}),
               RenderResourceDescriptorErrors::BufferInitialDataInvalid);

    const RenderBufferInitialDataView borrowed{bytes};
    bytes[0] = std::byte{0x7F};
    CHECK(borrowed.bytes.front() == std::byte{0x7F});
}

TEST_CASE("Texture descriptors accept core policy and reject malformed structure", "[runtime][renderer][resource-descriptor]") {
    CHECK(ValidateRenderTextureDescriptor(ColorTexture()).HasValue());
    CHECK(ValidateRenderTextureDescriptor({.dimension = RenderTextureDimension::OneD,
                                           .extent = {16, 1},
                                           .format = RenderTextureFormat::R16Float,
                                           .mipCount = 5,
                                           .usage = RenderTextureUsage::Sampled | RenderTextureUsage::CopyDestination})
              .HasValue());
    CHECK(ValidateRenderTextureDescriptor({.dimension = RenderTextureDimension::ThreeD,
                                           .extent = {8, 4},
                                           .format = RenderTextureFormat::Rgba16Float,
                                           .mipCount = 4,
                                           .usage = RenderTextureUsage::Storage,
                                           .depth = 4})
              .HasValue());
    CHECK(ValidateRenderTextureDescriptor({.extent = {64, 64},
                                           .format = RenderTextureFormat::Bgra8UnormSrgb,
                                           .sampleCount = 4,
                                           .usage = RenderTextureUsage::RenderAttachment})
              .HasValue());
}

TEST_CASE("Texture descriptors reject malformed structural policy", "[runtime][renderer][resource-descriptor]") {
    CheckError(ValidateRenderTextureDescriptor(ColorTexture({0, 2})), RenderResourceDescriptorErrors::TextureInvalid);

    auto invalid = ColorTexture();
    invalid.dimension = static_cast<RenderTextureDimension>(0xFFU);
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.format = static_cast<RenderTextureFormat>(0xFFU);
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.usage = static_cast<RenderTextureUsage>(0x80U);
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.mipCount = 0;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.mipCount = 3;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.sampleCount = 3;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.layerCount = std::numeric_limits<std::uint32_t>::max();
    invalid.mipCount = 2;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.dimension = RenderTextureDimension::OneD;
    invalid.extent.height = 2;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
    invalid = ColorTexture();
    invalid.dimension = RenderTextureDimension::ThreeD;
    invalid.layerCount = 2;
    invalid.depth = 2;
    CheckError(ValidateRenderTextureDescriptor(invalid), RenderResourceDescriptorErrors::TextureInvalid);
}

TEST_CASE("Sampler descriptors validate every enum and finite numeric boundary", "[runtime][renderer][resource-descriptor]") {
    constexpr RenderSamplerDescriptor valid{.minFilter = RenderSamplerFilter::Linear,
                                            .magFilter = RenderSamplerFilter::Linear,
                                            .mipmapMode = RenderSamplerMipmapMode::Linear,
                                            .addressU = RenderSamplerAddressMode::Repeat,
                                            .addressV = RenderSamplerAddressMode::MirroredRepeat,
                                            .addressW = RenderSamplerAddressMode::ClampToBorder,
                                            .minimumLod = 1.0F,
                                            .maximumLod = 4.0F,
                                            .maximumAnisotropy = 8.0F,
                                            .compareEnabled = true,
                                            .compare = RenderCompareFunction::LessEqual};
    CHECK(ValidateRenderSamplerDescriptor(valid).HasValue());
    STATIC_CHECK(valid.IsValid());

    auto invalid = valid;
    invalid.minFilter = static_cast<RenderSamplerFilter>(0xFFU);
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.mipmapMode = static_cast<RenderSamplerMipmapMode>(0xFFU);
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.addressW = static_cast<RenderSamplerAddressMode>(0xFFU);
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.compareEnabled = false;
    invalid.compare = static_cast<RenderCompareFunction>(0xFFU);
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.minimumLod = -1.0F;
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.maximumLod = 0.5F;
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.maximumLod = std::numeric_limits<float>::infinity();
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.maximumAnisotropy = std::numeric_limits<float>::quiet_NaN();
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
    invalid = valid;
    invalid.maximumAnisotropy = 0.5F;
    CheckError(ValidateRenderSamplerDescriptor(invalid), RenderResourceDescriptorErrors::SamplerInvalid);
}

TEST_CASE("Texture views validate structure separately from source compatibility", "[runtime][renderer][resource-descriptor]") {
    const RenderTextureDescriptor color = ColorTexture();
    const RenderTextureViewDescriptor colorView = ColorView();
    CHECK(ValidateRenderTextureViewDescriptor(colorView).HasValue());
    CHECK(ValidateRenderTextureViewCompatibility(color, colorView).HasValue());

    const RenderTextureDescriptor arrayTexture{.extent = {16, 16},
                                               .format = RenderTextureFormat::Rgba8UnormSrgb,
                                               .mipCount = 5,
                                               .layerCount = 12,
                                               .usage = RenderTextureUsage::Sampled};
    auto cubeArrayView = colorView;
    cubeArrayView.format = RenderTextureFormat::Rgba8Unorm;
    cubeArrayView.mipCount = 5;
    cubeArrayView.layerCount = 12;
    cubeArrayView.dimension = RenderTextureViewDimension::CubeArray;
    CHECK(ValidateRenderTextureViewCompatibility(arrayTexture, cubeArrayView).HasValue());
    auto cubeView = cubeArrayView;
    cubeView.dimension = RenderTextureViewDimension::Cube;
    cubeView.layerCount = 6;
    cubeView.baseLayer = 6;
    CHECK(ValidateRenderTextureViewCompatibility(arrayTexture, cubeView).HasValue());
    cubeArrayView.baseLayer = 1;
    CheckError(ValidateRenderTextureViewCompatibility(arrayTexture, cubeArrayView),
               RenderResourceDescriptorErrors::TextureViewIncompatible);
    cubeArrayView.baseLayer = 0;
    cubeArrayView.layerCount = 5;
    CheckError(ValidateRenderTextureViewCompatibility(arrayTexture, cubeArrayView),
               RenderResourceDescriptorErrors::TextureViewIncompatible);

    auto nonSquareArray = arrayTexture;
    nonSquareArray.extent.height = 8;
    cubeArrayView.layerCount = 12;
    CheckError(ValidateRenderTextureViewCompatibility(nonSquareArray, cubeArrayView),
               RenderResourceDescriptorErrors::TextureViewIncompatible);
    cubeView.baseLayer = 0;
    CheckError(ValidateRenderTextureViewCompatibility(nonSquareArray, cubeView), RenderResourceDescriptorErrors::TextureViewIncompatible);

    auto incompleteCubeArray = arrayTexture;
    incompleteCubeArray.layerCount = 13;
    CheckError(ValidateRenderTextureViewCompatibility(incompleteCubeArray, cubeView),
               RenderResourceDescriptorErrors::TextureViewIncompatible);
}

TEST_CASE("Texture views reject malformed ranges and incompatible aspects", "[runtime][renderer][resource-descriptor]") {
    const RenderTextureDescriptor color = ColorTexture();
    const RenderTextureViewDescriptor colorView = ColorView();

    auto invalid = colorView;
    invalid.texture = {};
    CheckError(ValidateRenderTextureViewDescriptor(invalid), RenderResourceDescriptorErrors::TextureViewInvalid);
    invalid = colorView;
    invalid.aspect = static_cast<RenderTextureAspect>(0xFFU);
    CheckError(ValidateRenderTextureViewDescriptor(invalid), RenderResourceDescriptorErrors::TextureViewInvalid);
    invalid = colorView;
    invalid.mipCount = 0;
    CheckError(ValidateRenderTextureViewDescriptor(invalid), RenderResourceDescriptorErrors::TextureViewInvalid);

    auto incompatible = colorView;
    incompatible.aspect = RenderTextureAspect::Depth;
    CheckError(ValidateRenderTextureViewCompatibility(color, incompatible), RenderResourceDescriptorErrors::TextureViewIncompatible);
    incompatible = colorView;
    incompatible.baseMip = std::numeric_limits<std::uint32_t>::max();
    CheckError(ValidateRenderTextureViewCompatibility(color, incompatible), RenderResourceDescriptorErrors::TextureViewIncompatible);
    incompatible = colorView;
    incompatible.baseLayer = 1;
    CheckError(ValidateRenderTextureViewCompatibility(color, incompatible), RenderResourceDescriptorErrors::TextureViewIncompatible);

    const RenderTextureDescriptor depthStencil{.extent = {2, 2},
                                               .format = RenderTextureFormat::Depth24Stencil8,
                                               .usage = RenderTextureUsage::RenderAttachment};
    auto depthView = colorView;
    depthView.format = RenderTextureFormat::Depth24Stencil8;
    depthView.aspect = RenderTextureAspect::DepthStencil;
    CHECK(ValidateRenderTextureViewCompatibility(depthStencil, depthView).HasValue());

    const RenderTextureDescriptor depth{.extent = {2, 2},
                                        .format = RenderTextureFormat::Depth32Float,
                                        .usage = RenderTextureUsage::RenderAttachment};
    depthView.format = RenderTextureFormat::Depth32Float;
    depthView.aspect = RenderTextureAspect::DepthStencil;
    CheckError(ValidateRenderTextureViewCompatibility(depth, depthView), RenderResourceDescriptorErrors::TextureViewIncompatible);

    const RenderTextureDescriptor depthStencil32{.extent = {2, 2},
                                                 .format = RenderTextureFormat::Depth32FloatStencil8,
                                                 .usage = RenderTextureUsage::RenderAttachment};
    depthView.format = RenderTextureFormat::Depth32FloatStencil8;
    depthView.aspect = RenderTextureAspect::Stencil;
    CHECK(ValidateRenderTextureViewCompatibility(depthStencil32, depthView).HasValue());
}

TEST_CASE("Texture initial data validates canonical subresources and checked pitches", "[runtime][renderer][resource-descriptor]") {
    const RenderTextureDescriptor descriptor = ColorTexture();
    std::array<std::byte, 16> bytes{};
    const RenderTextureSubresourceInitialDataView subresource{.rowPitchBytes = 8, .slicePitchBytes = 16, .bytes = bytes};
    const std::array subresources{subresource};
    CHECK(ValidateRenderTextureInitialData(descriptor, {}).HasValue());
    CHECK(ValidateRenderTextureInitialData(descriptor, {subresources}).HasValue());

    const RenderTextureDescriptor arrayTexture{.extent = {2, 2},
                                               .format = RenderTextureFormat::Rgba8Unorm,
                                               .mipCount = 2,
                                               .layerCount = 2,
                                               .usage = RenderTextureUsage::CopyDestination};
    std::array<std::byte, 4> mipBytes{};
    const std::array arraySubresources{
        RenderTextureSubresourceInitialDataView{.mipLevel = 0, .arrayLayer = 0, .rowPitchBytes = 8, .slicePitchBytes = 16, .bytes = bytes},
        RenderTextureSubresourceInitialDataView{.mipLevel = 1,
                                                .arrayLayer = 0,
                                                .rowPitchBytes = 4,
                                                .slicePitchBytes = 4,
                                                .bytes = mipBytes},
        RenderTextureSubresourceInitialDataView{.mipLevel = 0, .arrayLayer = 1, .rowPitchBytes = 8, .slicePitchBytes = 16, .bytes = bytes},
        RenderTextureSubresourceInitialDataView{.mipLevel = 1,
                                                .arrayLayer = 1,
                                                .rowPitchBytes = 4,
                                                .slicePitchBytes = 4,
                                                .bytes = mipBytes},
    };
    CHECK(ValidateRenderTextureInitialData(arrayTexture, {arraySubresources}).HasValue());
    auto wrongOrder = arraySubresources;
    wrongOrder[1].arrayLayer = 1;
    CheckError(ValidateRenderTextureInitialData(arrayTexture, {wrongOrder}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);

    auto invalid = subresource;
    invalid.mipLevel = 1;
    const std::array wrongMip{invalid};
    CheckError(ValidateRenderTextureInitialData(descriptor, {wrongMip}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);
    invalid = subresource;
    invalid.rowPitchBytes = 7;
    const std::array shortRow{invalid};
    CheckError(ValidateRenderTextureInitialData(descriptor, {shortRow}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);
    invalid = subresource;
    invalid.slicePitchBytes = 15;
    const std::array shortSlice{invalid};
    CheckError(ValidateRenderTextureInitialData(descriptor, {shortSlice}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);
    invalid = subresource;
    invalid.bytes = std::span{bytes}.first<15>();
    const std::array shortBytes{invalid};
    CheckError(ValidateRenderTextureInitialData(descriptor, {shortBytes}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);
}

TEST_CASE("Texture initial data rejects overflow and accepts volume layout", "[runtime][renderer][resource-descriptor]") {
    const RenderTextureDescriptor tall = ColorTexture({1, std::numeric_limits<std::uint32_t>::max()});
    std::array<std::byte, 1> sentinel{};
    const RenderTextureSubresourceInitialDataView overflowing{
        .rowPitchBytes = std::numeric_limits<std::size_t>::max(),
        .slicePitchBytes = std::numeric_limits<std::size_t>::max(),
        .bytes = sentinel,
    };
    const std::array overflowRecords{overflowing};
    CheckError(ValidateRenderTextureInitialData(tall, {overflowRecords}), RenderResourceDescriptorErrors::TextureInitialDataInvalid);

    const RenderTextureDescriptor volume{.dimension = RenderTextureDimension::ThreeD,
                                         .extent = {2, 2},
                                         .format = RenderTextureFormat::R8Unorm,
                                         .usage = RenderTextureUsage::CopyDestination,
                                         .depth = 2};
    std::array<std::byte, 8> volumeBytes{};
    const std::array volumeRecords{RenderTextureSubresourceInitialDataView{.rowPitchBytes = 2, .slicePitchBytes = 4, .bytes = volumeBytes}};
    CHECK(ValidateRenderTextureInitialData(volume, {volumeRecords}).HasValue());
}
