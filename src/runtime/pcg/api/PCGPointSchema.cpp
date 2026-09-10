#include "Horo/PCG/PCGPointSchema.h"

#include "Horo/PCG/PCGErrors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <type_traits>

namespace Horo::PCG {
    struct PCGPointStorage::State final {
        std::shared_ptr<const PCGPointSchema> schema;
        PCGPointCoreColumns core;
        std::vector<PCGAttributeColumn> attributes;
    };

    namespace {
        constexpr std::size_t MiB = 1024U * 1024U;

        [[nodiscard]] Error Failure(const ErrorCodeDescriptor &descriptor) {
            return MakeError(descriptor);
        }

        [[nodiscard]] bool IsKnown(const PCGOperationalTier tier) noexcept {
            switch (tier) {
                case PCGOperationalTier::Baseline:
                case PCGOperationalTier::Standard:
                case PCGOperationalTier::High:
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool IsKnown(const PCGAttributeType type) noexcept {
            switch (type) {
                case PCGAttributeType::Boolean:
                case PCGAttributeType::SignedInteger:
                case PCGAttributeType::UnsignedInteger:
                case PCGAttributeType::Scalar:
                case PCGAttributeType::Vector2:
                case PCGAttributeType::Vector3:
                case PCGAttributeType::Vector4:
                    return true;
            }
            return false;
        }

        [[nodiscard]] std::size_t CanonicalBytes(const PCGAttributeType type) noexcept {
            switch (type) {
                case PCGAttributeType::Boolean:
                    return 1;
                case PCGAttributeType::SignedInteger:
                case PCGAttributeType::UnsignedInteger:
                case PCGAttributeType::Scalar:
                    return 8;
                case PCGAttributeType::Vector2:
                    return 8;
                case PCGAttributeType::Vector3:
                    return 12;
                case PCGAttributeType::Vector4:
                    return 16;
            }
            return 0;
        }

        [[nodiscard]] PCGAttributeType TypeOf(const PCGAttributeColumnValues &values) noexcept {
            return std::visit([]<typename Column>(const Column &) {
                using T = std::remove_cvref_t<Column>;
                if constexpr (std::is_same_v<T, PCGBoolColumn>)
                    return PCGAttributeType::Boolean;
                if constexpr (std::is_same_v<T, PCGSignedIntegerColumn>)
                    return PCGAttributeType::SignedInteger;
                if constexpr (std::is_same_v<T, PCGUnsignedIntegerColumn>)
                    return PCGAttributeType::UnsignedInteger;
                if constexpr (std::is_same_v<T, PCGScalarColumn>)
                    return PCGAttributeType::Scalar;
                if constexpr (std::is_same_v<T, PCGVector2Column>)
                    return PCGAttributeType::Vector2;
                if constexpr (std::is_same_v<T, PCGVector3Column>)
                    return PCGAttributeType::Vector3;
                return PCGAttributeType::Vector4;
            }, values);
        }

        [[nodiscard]] std::size_t CountOf(const PCGAttributeColumnValues &values) noexcept {
            return std::visit([](const auto &column) {
                return column.size();
            }, values);
        }

        [[nodiscard]] constexpr bool IsLowerAscii(const unsigned char byte) noexcept {
            return byte >= 'a' && byte <= 'z';
        }

        [[nodiscard]] constexpr bool IsKeyTail(const unsigned char byte) noexcept {
            return IsLowerAscii(byte) || (byte >= '0' && byte <= '9') || byte == '_';
        }

        [[nodiscard]] bool IsCanonicalSegment(const std::string_view segment) noexcept {
            return !segment.empty() && IsLowerAscii(static_cast<unsigned char>(segment.front())) &&
                   std::ranges::all_of(segment.substr(1), [](const unsigned char byte) {
                return IsKeyTail(byte);
            });
        }

        [[nodiscard]] bool IsCanonicalKey(const std::string_view value) noexcept {
            if (value.empty() || value.size() > MaximumAttributeKeyBytes || value.find('.') == std::string_view::npos)
                return false;
            std::size_t start{};
            while (start <= value.size()) {
                const std::size_t separator = value.find('.', start);
                const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
                if (!IsCanonicalSegment(value.substr(start, end - start)))
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                start = separator + 1;
            }
            return false;
        }

        [[nodiscard]] bool ValuesAreValid(const PCGAttributeColumnValues &values) noexcept {
            return std::visit([]<typename Column>(const Column &column) {
                using T = typename Column::value_type;
                if constexpr (std::is_same_v<T, std::uint8_t>)
                    return std::ranges::all_of(column, [](const auto value) {
                        return value <= 1;
                    });
                if constexpr (std::is_same_v<T, double>)
                    return std::ranges::all_of(column, [](const auto value) {
                        return std::isfinite(value);
                    });
                if constexpr (std::is_same_v<T, Math::Vec2> || std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                    return std::ranges::all_of(column, [](const auto value) {
                        return Math::IsFinite(value);
                    });
                return true;
            }, values);
        }

        [[nodiscard]] Result<std::size_t> AccountBytes(const PCGPointStorageCandidate &candidate, const std::size_t count) {
            auto corePerPoint = CheckedPCGAdd(sizeof(Math::Transform), sizeof(Math::Aabb));
            if (corePerPoint.HasError())
                return corePerPoint;
            corePerPoint = CheckedPCGAdd(corePerPoint.Value(), sizeof(float) + sizeof(std::uint64_t));
            if (corePerPoint.HasError())
                return corePerPoint;
            auto total = CheckedPCGMultiply(count, corePerPoint.Value());
            if (total.HasError())
                return total;
            for (const auto &column : candidate.attributes) {
                const auto bytes = CheckedPCGMultiply(count, CanonicalBytes(TypeOf(column.values)));
                if (bytes.HasError())
                    return bytes;
                total = CheckedPCGAdd(total.Value(), bytes.Value());
                if (total.HasError())
                    return total;
            }
            return total;
        }

        [[nodiscard]] Result<void> ValidateCore(const PCGPointCoreColumns &core) {
            const std::size_t count = core.transforms.size();
            if (core.bounds.size() != count || core.densities.size() != count || core.seeds.size() != count)
                return Result<void>::Failure(Failure(PCGErrors::PointDataInvalid));
            for (std::size_t index = 0; index < count; ++index) {
                if (core.transforms[index].TryToMatrix().HasError() || !core.bounds[index].IsValid() ||
                    !std::isfinite(core.densities[index]) || core.densities[index] < 0.0F || core.densities[index] > 1.0F)
                    return Result<void>::Failure(Failure(PCGErrors::PointDataInvalid));
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateColumns(const PCGPointSchema &schema, std::vector<PCGAttributeColumn> &columns,
                                                   const std::size_t count) {
            if (columns.size() != schema.Attributes().size())
                return Result<void>::Failure(Failure(PCGErrors::PointDataInvalid));
            std::ranges::sort(columns, {}, &PCGAttributeColumn::key);
            for (std::size_t index = 0; index < columns.size(); ++index) {
                const auto &column = columns[index];
                if (index > 0 && columns[index - 1].key == column.key)
                    return Result<void>::Failure(Failure(PCGErrors::PointAttributeDuplicate));
                const auto *field = schema.Find(column.key);
                if (field == nullptr)
                    return Result<void>::Failure(Failure(PCGErrors::PointAttributeUnknown));
                if (field->type != TypeOf(column.values))
                    return Result<void>::Failure(Failure(PCGErrors::PointAttributeTypeMismatch));
                if (CountOf(column.values) != count || !ValuesAreValid(column.values))
                    return Result<void>::Failure(Failure(PCGErrors::PointDataInvalid));
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc LimitsForTier */
    Result<PCGTierLimits> LimitsForTier(const PCGOperationalTier tier) {
        switch (tier) {
            case PCGOperationalTier::Baseline:
                return Result<PCGTierLimits>::Success({32, 64, 32, 64, 1, 16, 256, 16'384, 65'536, 16'384, 2 * MiB, 16 * MiB, 16 * MiB,
                                                       32 * MiB, 16 * MiB, 32 * MiB, 112 * MiB});
            case PCGOperationalTier::Standard:
                return Result<PCGTierLimits>::Success({256, 512, 128, 512, 1, 32, 512, 262'144, 1'048'576, 262'144, 16 * MiB, 128 * MiB,
                                                       128 * MiB, 256 * MiB, 256 * MiB, 256 * MiB, 1'024 * MiB});
            case PCGOperationalTier::High:
                return Result<PCGTierLimits>::Success({1'024, 2'048, 512, 2'048, 1, 64, 1'024, 2'097'152, 8'388'608, 2'097'152, 64 * MiB,
                                                       512 * MiB, 512 * MiB, 1'024 * MiB, 1'024 * MiB, 1'024 * MiB, 4'096ULL * MiB});
        }
        return Result<PCGTierLimits>::Failure(Failure(PCGErrors::PointSchemaInvalid));
    }

    /** @copydoc PCGAttributeKey::Create */
    Result<PCGAttributeKey> PCGAttributeKey::Create(const std::string_view value) {
        if (!IsCanonicalKey(value))
            return Result<PCGAttributeKey>::Failure(Failure(PCGErrors::PointSchemaInvalid));
        try {
            return Result<PCGAttributeKey>::Success(PCGAttributeKey{std::string(value)});
        } catch (...) {
            return Result<PCGAttributeKey>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        }
    }

    /** @copydoc PCGPointSchema::Capture */
    Result<PCGPointSchema> PCGPointSchema::Capture(const PCGPointSchemaDescriptor &descriptor) {
        auto limits = LimitsForTier(descriptor.tier);
        if (limits.HasError())
            return Result<PCGPointSchema>::Failure(limits.ErrorValue());
        if (descriptor.attributes.size() > limits.Value().maximumAttributesPerPoint)
            return Result<PCGPointSchema>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        std::size_t bytes{};
        try {
            std::vector<PCGAttributeDescriptor> attributes(descriptor.attributes.begin(), descriptor.attributes.end());
            for (const auto &attribute : attributes) {
                if (!IsKnown(attribute.type))
                    return Result<PCGPointSchema>::Failure(Failure(PCGErrors::PointSchemaInvalid));
                auto next = CheckedPCGAdd(bytes, CanonicalBytes(attribute.type));
                if (next.HasError())
                    return Result<PCGPointSchema>::Failure(next.ErrorValue());
                bytes = next.Value();
            }
            if (bytes > limits.Value().maximumAttributeValueBytesPerPoint)
                return Result<PCGPointSchema>::Failure(Failure(PCGErrors::PointCapacityExceeded));
            std::ranges::sort(attributes, {}, &PCGAttributeDescriptor::key);
            if (std::adjacent_find(attributes.begin(), attributes.end(), [](const auto &left, const auto &right) {
                return left.key == right.key;
            }) != attributes.end())
                return Result<PCGPointSchema>::Failure(Failure(PCGErrors::PointAttributeDuplicate));
            return Result<PCGPointSchema>::Success(PCGPointSchema{descriptor.tier, std::move(attributes), bytes});
        } catch (...) {
            return Result<PCGPointSchema>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        }
    }

    /** @copydoc PCGPointSchema::Find */
    const PCGAttributeDescriptor *PCGPointSchema::Find(const std::string_view canonicalKey) const noexcept {
        const auto found = std::ranges::lower_bound(attributes_, canonicalKey, {}, [](const auto &item) {
            return item.key.Value();
        });
        return found != attributes_.end() && found->key.Value() == canonicalKey ? &*found : nullptr;
    }

    /** @copydoc PCGPointStorage::PointCount */
    std::size_t PCGPointStorage::PointCount() const noexcept {
        return state_->core.transforms.size();
    }

    /** @copydoc PCGPointStorage::Schema */
    const PCGPointSchema &PCGPointStorage::Schema() const noexcept {
        return *state_->schema;
    }

    /** @copydoc PCGPointStorage::Transforms */
    std::span<const Math::Transform> PCGPointStorage::Transforms() const noexcept {
        return state_->core.transforms;
    }

    /** @copydoc PCGPointStorage::Bounds */
    std::span<const Math::Aabb> PCGPointStorage::Bounds() const noexcept {
        return state_->core.bounds;
    }

    /** @copydoc PCGPointStorage::Densities */
    std::span<const float> PCGPointStorage::Densities() const noexcept {
        return state_->core.densities;
    }

    /** @copydoc PCGPointStorage::Seeds */
    std::span<const std::uint64_t> PCGPointStorage::Seeds() const noexcept {
        return state_->core.seeds;
    }

    const PCGAttributeColumnValues *PCGPointStorage::FindColumnValues(const std::string_view key) const noexcept {
        const auto found = std::ranges::lower_bound(state_->attributes, key, {}, &PCGAttributeColumn::key);
        return found != state_->attributes.end() && found->key == key ? &found->values : nullptr;
    }

    /** @copydoc CapturePointStorage */
    Result<std::shared_ptr<const PCGPointStorage>> CapturePointStorage(PCGPointStorageCandidate candidate) {
        if (candidate.schema == nullptr)
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(Failure(PCGErrors::PointSchemaInvalid));
        const std::size_t count = candidate.core.transforms.size();
        const auto limits = LimitsForTier(candidate.schema->Tier()).Value();
        if (count > limits.maximumPointsPerNodeOutput)
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        if (const auto core = ValidateCore(candidate.core); core.HasError())
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(core.ErrorValue());
        if (const auto columns = ValidateColumns(*candidate.schema, candidate.attributes, count); columns.HasError())
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(columns.ErrorValue());
        const auto bytes = AccountBytes(candidate, count);
        if (bytes.HasError())
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(bytes.ErrorValue());
        if (bytes.Value() > limits.maximumCandidateBytes)
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        try {
            auto state = std::make_shared<const PCGPointStorage::State>(
                PCGPointStorage::State{std::move(candidate.schema), std::move(candidate.core), std::move(candidate.attributes)});
            return Result<std::shared_ptr<const PCGPointStorage>>::Success(
                std::shared_ptr<const PCGPointStorage>{new PCGPointStorage(std::move(state))});
        } catch (...) {
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(Failure(PCGErrors::PointCapacityExceeded));
        }
    }

    /** @copydoc ReplacePointStorage */
    Result<std::shared_ptr<const PCGPointStorage>> ReplacePointStorage(const std::shared_ptr<const PCGPointStorage> &current,
                                                                       PCGPointStorageCandidate candidate) {
        if (current == nullptr)
            return Result<std::shared_ptr<const PCGPointStorage>>::Failure(Failure(PCGErrors::PointDataInvalid));
        return CapturePointStorage(std::move(candidate));
    }

    /** @copydoc CheckedPCGAdd */
    Result<std::size_t> CheckedPCGAdd(const std::size_t left, const std::size_t right) {
        if (right > std::numeric_limits<std::size_t>::max() - left)
            return Result<std::size_t>::Failure(Failure(PCGErrors::PointSizeOverflow));
        return Result<std::size_t>::Success(left + right);
    }

    /** @copydoc CheckedPCGMultiply */
    Result<std::size_t> CheckedPCGMultiply(const std::size_t left, const std::size_t right) {
        if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
            return Result<std::size_t>::Failure(Failure(PCGErrors::PointSizeOverflow));
        return Result<std::size_t>::Success(left * right);
    }

    /** @copydoc CheckedPCGAlignUp */
    Result<std::size_t> CheckedPCGAlignUp(const std::size_t value, const std::size_t alignment) {
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return Result<std::size_t>::Failure(Failure(PCGErrors::PointSizeOverflow));
        const std::size_t mask = alignment - 1;
        auto added = CheckedPCGAdd(value, mask);
        if (added.HasError())
            return added;
        return Result<std::size_t>::Success(added.Value() & ~mask);
    }
}  // namespace Horo::PCG
