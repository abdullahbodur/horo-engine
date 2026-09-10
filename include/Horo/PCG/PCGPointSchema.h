#pragma once

/**
 * @file PCGPointSchema.h
 * @brief Immutable typed PCG point schemas, columnar storage, and operational tier limits.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Horo::PCG {
    /** @brief Maximum canonical UTF-8 bytes in one schema-1 attribute key. */
    inline constexpr std::size_t MaximumAttributeKeyBytes = 96;

    /** @brief Provider-neutral PCG operational profile fixed by ADR-156. */
    enum class PCGOperationalTier : std::uint8_t {
        Baseline,
        Standard,
        High
    };

    /** @brief Exact structural, point, payload, and memory ceilings for one operational profile. */
    struct PCGTierLimits final {
        std::size_t maximumNodes{};
        std::size_t maximumEdges{};
        std::size_t maximumExposedInputs{};
        std::size_t maximumDependencies{};
        std::size_t maximumDependencyDepth{};
        std::size_t maximumAttributesPerPoint{};
        std::size_t maximumAttributeValueBytesPerPoint{};
        std::size_t maximumPointsPerNodeOutput{};
        std::size_t maximumMaterializedPointRecords{};
        std::size_t maximumOutputIntents{};
        std::size_t maximumPlanAndAuxiliaryBytes{};
        std::size_t maximumResidentPlanBytes{};
        std::size_t maximumInputSnapshotBytes{};
        std::size_t maximumScratchBytes{};
        std::size_t maximumCandidateBytes{};
        std::size_t maximumReplacementOverlapBytes{};
        std::size_t maximumAggregateBytes{};

        [[nodiscard]] constexpr auto operator<=>(const PCGTierLimits &) const noexcept = default;
    };

    /** @brief Returns exact ADR-156 limits for a known operational tier. */
    [[nodiscard]] Result<PCGTierLimits> LimitsForTier(PCGOperationalTier tier);

    /** @brief Closed schema-1 PCG attribute value vocabulary. */
    enum class PCGAttributeType : std::uint8_t {
        Boolean,
        SignedInteger,
        UnsignedInteger,
        Scalar,
        Vector2,
        Vector3,
        Vector4
    };

    /** @brief Validated lowercase ASCII namespaced attribute identity. */
    class PCGAttributeKey final {
    public:
        PCGAttributeKey() = delete;

        /** @brief Validates and owns a canonical key. @param value Candidate `namespace.name` key. @return Key or typed failure. */
        [[nodiscard]] static Result<PCGAttributeKey> Create(std::string_view value);

        /** @brief Returns canonical owned bytes. */
        [[nodiscard]] std::string_view Value() const noexcept {
            return value_;
        }

        [[nodiscard]] auto operator<=>(const PCGAttributeKey &) const noexcept = default;

    private:
        explicit PCGAttributeKey(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief One immutable schema field. */
    struct PCGAttributeDescriptor final {
        PCGAttributeKey key;
        PCGAttributeType type{PCGAttributeType::Boolean};
        [[nodiscard]] auto operator<=>(const PCGAttributeDescriptor &) const noexcept = default;
    };

    /** @brief Borrowed unsorted schema candidate. */
    struct PCGPointSchemaDescriptor final {
        PCGOperationalTier tier{PCGOperationalTier::Baseline};
        std::span<const PCGAttributeDescriptor> attributes;
    };

    /** @brief Immutable canonical key-sorted schema. */
    class PCGPointSchema final {
    public:
        PCGPointSchema() = delete;
        /** @brief Validates and captures a schema candidate. */
        [[nodiscard]] static Result<PCGPointSchema> Capture(const PCGPointSchemaDescriptor &descriptor);

        [[nodiscard]] PCGOperationalTier Tier() const noexcept {
            return tier_;
        }

        [[nodiscard]] std::span<const PCGAttributeDescriptor> Attributes() const noexcept {
            return attributes_;
        }

        /** @brief Finds a canonical key without allocating. @return Descriptor or null when absent. */
        [[nodiscard]] const PCGAttributeDescriptor *Find(std::string_view canonicalKey) const noexcept;

        [[nodiscard]] std::size_t CanonicalValueBytesPerPoint() const noexcept {
            return valueBytesPerPoint_;
        }

    private:
        PCGPointSchema(PCGOperationalTier tier, std::vector<PCGAttributeDescriptor> attributes, std::size_t valueBytes)
            : tier_(tier), attributes_(std::move(attributes)), valueBytesPerPoint_(valueBytes) {}

        PCGOperationalTier tier_;
        std::vector<PCGAttributeDescriptor> attributes_;
        std::size_t valueBytesPerPoint_{};
    };

    using PCGBoolColumn = std::vector<std::uint8_t>;
    using PCGSignedIntegerColumn = std::vector<std::int64_t>;
    using PCGUnsignedIntegerColumn = std::vector<std::uint64_t>;
    using PCGScalarColumn = std::vector<double>;
    using PCGVector2Column = std::vector<Math::Vec2>;
    using PCGVector3Column = std::vector<Math::Vec3>;
    using PCGVector4Column = std::vector<Math::Vec4>;
    /** @brief One homogeneous owned structure-of-arrays attribute column. */
    using PCGAttributeColumnValues = std::variant<PCGBoolColumn, PCGSignedIntegerColumn, PCGUnsignedIntegerColumn, PCGScalarColumn,
                                                  PCGVector2Column, PCGVector3Column, PCGVector4Column>;

    /** @brief Borrowed-name, owned-values candidate column. */
    struct PCGAttributeColumn final {
        std::string key;
        PCGAttributeColumnValues values;
    };

    /** @brief Core point values stored as parallel arrays rather than per-point maps. */
    struct PCGPointCoreColumns final {
        std::vector<Math::Transform> transforms;
        std::vector<Math::Aabb> bounds;
        std::vector<float> densities;
        std::vector<std::uint64_t> seeds;
    };

    /** @brief Complete detached point-storage candidate. */
    struct PCGPointStorageCandidate final {
        std::shared_ptr<const PCGPointSchema> schema;
        PCGPointCoreColumns core;
        std::vector<PCGAttributeColumn> attributes;
    };

    /** @brief Immutable validated point-column snapshot safe for concurrent readers. */
    class PCGPointStorage final {
    public:
        struct State;

        /** @brief Opaque construction gate restricted to validated capture. */
        class ConstructionKey final {
            friend Result<std::shared_ptr<const PCGPointStorage>> CapturePointStorage(PCGPointStorageCandidate candidate);
            ConstructionKey() = default;
        };

        PCGPointStorage() = delete;

        /** @brief Adopts a fully validated immutable state through the capture-only construction gate. */
        explicit PCGPointStorage(ConstructionKey, std::shared_ptr<const State> state) : state_(std::move(state)) {}

        [[nodiscard]] std::size_t PointCount() const noexcept;
        [[nodiscard]] const PCGPointSchema &Schema() const noexcept;
        [[nodiscard]] std::span<const Math::Transform> Transforms() const noexcept;
        [[nodiscard]] std::span<const Math::Aabb> Bounds() const noexcept;
        [[nodiscard]] std::span<const float> Densities() const noexcept;
        [[nodiscard]] std::span<const std::uint64_t> Seeds() const noexcept;

        /** @brief Returns a column only when both key and requested C++ column type match. */
        template <typename Column> [[nodiscard]] const Column *FindColumn(std::string_view key) const noexcept {
            const PCGAttributeColumnValues *values = FindColumnValues(key);
            return values == nullptr ? nullptr : std::get_if<Column>(values);
        }

    private:
        [[nodiscard]] const PCGAttributeColumnValues *FindColumnValues(std::string_view key) const noexcept;
        std::shared_ptr<const State> state_;
    };

    /** @brief Validates a detached candidate completely before returning an immutable snapshot. */
    [[nodiscard]] Result<std::shared_ptr<const PCGPointStorage>> CapturePointStorage(PCGPointStorageCandidate candidate);
    /** @brief Validates replacement without modifying or invalidating the current immutable snapshot. */
    [[nodiscard]] Result<std::shared_ptr<const PCGPointStorage>> ReplacePointStorage(const std::shared_ptr<const PCGPointStorage> &current,
                                                                                     PCGPointStorageCandidate candidate);

    /** @brief Checked size addition used by PCG byte admission. */
    [[nodiscard]] Result<std::size_t> CheckedPCGAdd(std::size_t left, std::size_t right);
    /** @brief Checked size multiplication used by PCG byte admission. */
    [[nodiscard]] Result<std::size_t> CheckedPCGMultiply(std::size_t left, std::size_t right);
    /** @brief Checked power-of-two upward alignment used by PCG byte admission. */
    [[nodiscard]] Result<std::size_t> CheckedPCGAlignUp(std::size_t value, std::size_t alignment);
}  // namespace Horo::PCG
