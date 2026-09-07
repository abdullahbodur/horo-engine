#pragma once

/**
 * @file ShaderManifest.h
 * @brief Backend-neutral shader manifest, binding layout, and target requirement contracts.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Foundation/Sha256.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Horo::Render {
    /** @brief Stable logical shader resource identity independent of native binding positions. */
    struct ShaderBindingId {
        std::uint32_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const ShaderBindingId &) const noexcept = default;
    };

    /** @brief Stable logical material or shader parameter identity. */
    struct ShaderParameterId {
        std::uint32_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const ShaderParameterId &) const noexcept = default;
    };

    /** @brief Stable logical specialization input identity. */
    struct ShaderSpecializationId {
        std::uint32_t value{0};

        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const ShaderSpecializationId &) const noexcept = default;
    };

    /** @brief Shader execution stage represented without native API values. */
    enum class ShaderStage : std::uint8_t {
        Vertex,
        Fragment,
        Compute
    };

    /** @brief Stage visibility mask for bindings and constants. */
    enum class ShaderStageVisibility : std::uint8_t {
        None = 0,
        Vertex = 1U << 0U,
        Fragment = 1U << 1U,
        Compute = 1U << 2U,
    };

    [[nodiscard]] constexpr ShaderStageVisibility operator|(const ShaderStageVisibility left, const ShaderStageVisibility right) noexcept {
        return static_cast<ShaderStageVisibility>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    /** @brief Logical resource category named by a shader binding. */
    enum class ShaderResourceKind : std::uint8_t {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler
    };

    /** @brief Logical access requested from a bound resource. */
    enum class ShaderResourceAccess : std::uint8_t {
        ReadOnly,
        ReadWrite
    };

    /** @brief Portable scalar category for parameters and specialization inputs. */
    enum class ShaderValueType : std::uint8_t {
        Float32,
        Int32,
        Uint32,
        Bool32
    };

    /** @brief One named source entry point and its exact execution stage. */
    struct ShaderEntryPoint {
        ShaderStage stage{ShaderStage::Vertex};
        std::string name;
    };

    /** @brief One logical resource independent of descriptor sets, registers, argument buffers, and native names. */
    struct ShaderResourceBinding {
        ShaderBindingId id;
        ShaderResourceKind kind{ShaderResourceKind::UniformBuffer};
        ShaderResourceAccess access{ShaderResourceAccess::ReadOnly};
        std::uint32_t arrayCount{1};
        ShaderStageVisibility stages{ShaderStageVisibility::None};
    };

    /** @brief One logical parameter belonging to a declared buffer binding. */
    struct ShaderParameter {
        ShaderParameterId id;
        ShaderBindingId binding;
        ShaderValueType type{ShaderValueType::Float32};
        std::uint8_t rows{1};
        std::uint8_t columns{1};
        std::uint32_t arrayCount{1};
    };

    /** @brief Portable inline-constant range lowered to push or root constants by a target map. */
    struct ShaderInlineConstantRange {
        std::uint32_t byteOffset{0};
        std::uint32_t byteSize{0};
        ShaderStageVisibility stages{ShaderStageVisibility::None};
    };

    /** @brief One typed specialization input with canonical 32-bit default-value bits. */
    struct ShaderSpecializationInput {
        ShaderSpecializationId id;
        ShaderValueType type{ShaderValueType::Uint32};
        std::uint32_t defaultValueBits{0};
        ShaderStageVisibility stages{ShaderStageVisibility::None};
    };

    /** @brief Backend family for one explicit cook/admission target. */
    enum class ShaderTargetBackend : std::uint8_t {
        Null,
        OpenGL,
        Vulkan,
        Metal,
        D3D12
    };

    /** @brief Target payload contract produced by the owning shader toolchain route. */
    enum class ShaderPayloadFormat : std::uint8_t {
        ValidationFixture,
        Glsl410,
        SpirV16,
        MetalLibrary24,
        Dxil60
    };

    /** @brief Explicit backend-neutral target capacity and feature requirements. */
    struct ShaderTargetRequirement {
        ShaderTargetBackend backend{ShaderTargetBackend::Null};
        ShaderPayloadFormat payloadFormat{ShaderPayloadFormat::ValidationFixture};
        std::uint32_t descriptorVersion{0};
        std::uint32_t interfaceSchemaVersion{0};
        std::uint32_t maximumBindings{0};
        std::uint32_t maximumInlineConstantBytes{0};
        bool supportsCompute{false};
        bool supportsStorageResources{false};
    };

    /** @brief Finite validation envelope supplied by the manifest owner. */
    struct ShaderManifestLimits {
        std::size_t maximumEntryPoints{8};
        std::size_t maximumBindings{128};
        std::size_t maximumParameters{512};
        std::size_t maximumInlineConstantRanges{16};
        std::size_t maximumSpecializationInputs{64};
        std::size_t maximumTargets{8};
        std::uint32_t maximumInlineConstantBytes{4'096};
        std::size_t maximumIdentityBytes{128};
    };

    /**
     * @brief Versioned inert shader interface metadata with wholly owned strings and collections.
     *
     * Validation is synchronous and bounded by ShaderManifestLimits. The value has no callbacks,
     * ambient registration, thread affinity, background work, or native/backend placement. It
     * therefore requires no cancellation or shutdown protocol and may be copied across those
     * lifecycle boundaries as immutable metadata.
     */
    struct ShaderManifest {
        std::uint32_t schemaVersion{0};
        std::string sourceIdentity;
        std::uint64_t sourceRevision{0};
        std::vector<ShaderEntryPoint> entryPoints;
        std::vector<ShaderResourceBinding> bindings;
        std::vector<ShaderParameter> parameters;
        std::vector<ShaderInlineConstantRange> inlineConstants;
        std::vector<ShaderSpecializationInput> specializationInputs;
        std::vector<ShaderTargetRequirement> targets;
    };

    /** @brief Canonical interface digest excluding source revision and backend/native placement. */
    struct ShaderInterfaceCompatibilityId {
        Sha256Digest digest;
        [[nodiscard]] auto operator<=>(const ShaderInterfaceCompatibilityId &) const = default;
    };

    /**
     * @brief Validates a complete manifest under finite caller-owned bounds.
     * @param manifest Immutable owned manifest metadata.
     * @param limits Finite validation envelope.
     * @return Success or a stable ShaderManifestErrors failure; no backend or native work is performed.
     */
    [[nodiscard]] Result<void> ValidateShaderManifest(const ShaderManifest &manifest, const ShaderManifestLimits &limits = {});

    /**
     * @brief Computes the canonical logical-interface compatibility identity.
     * @param manifest A manifest that must pass ValidateShaderManifest.
     * @param limits Finite validation envelope used before hashing.
     * @return Stable digest or the corresponding validation/allocation failure.
     */
    [[nodiscard]] Result<ShaderInterfaceCompatibilityId> ComputeShaderInterfaceCompatibilityId(const ShaderManifest &manifest,
                                                                                               const ShaderManifestLimits &limits = {});
}  // namespace Horo::Render
