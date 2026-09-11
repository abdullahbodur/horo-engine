#include "Horo/PCG/PCGGraphAsset.h"

#include "Horo/PCG/PCGErrors.h"
#include "PCGGraphAssetInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Horo::PCG {
    namespace {
        constexpr std::array<std::uint8_t, 4> GraphMagic{'H', 'P', 'C', 'G'};

        [[nodiscard]] Error Failure(const ErrorCodeDescriptor &descriptor) {
            return MakeError(descriptor);
        }

        template <typename T> [[nodiscard]] Result<T> Failed(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(Failure(descriptor));
        }

        [[nodiscard]] bool IsKnown(const PCGOperationalTier value) noexcept {
            using enum PCGOperationalTier;
            return value == Baseline || value == Standard || value == High;
        }

        template <typename Enum> [[nodiscard]] bool IsKnown(const Enum value) noexcept {
            return value < Enum::Count;
        }

        [[nodiscard]] bool IsValidLimits(const PCGGraphSourceLimits &limits, const PCGTierLimits &tier) noexcept {
            return limits.maximumSourceBytes > 0 && limits.maximumSourceBytes <= PCGGraphSourceHardLimits::SourceBytes &&
                   limits.maximumSourceBytes <= tier.maximumPlanAndAuxiliaryBytes && limits.maximumNodes > 0 &&
                   limits.maximumNodes <= tier.maximumNodes && limits.maximumEdges > 0 && limits.maximumEdges <= tier.maximumEdges &&
                   limits.maximumExposedInputs > 0 && limits.maximumExposedInputs <= tier.maximumExposedInputs &&
                   limits.maximumIdentifierBytes > 0 && limits.maximumIdentifierBytes <= PCGGraphSourceHardLimits::IdentifierBytes &&
                   limits.maximumPinsPerNode > 0 && limits.maximumPinsPerNode <= PCGGraphSourceHardLimits::PinsPerNode &&
                   limits.maximumTotalPins > 0 && limits.maximumTotalPins <= PCGGraphSourceHardLimits::TotalPins &&
                   limits.maximumNodePayloadBytes <= PCGGraphSourceHardLimits::NodePayloadBytes;
        }

        [[nodiscard]] bool IsDefaultLimits(const PCGGraphSourceLimits &limits) noexcept {
            return limits == PCGGraphSourceLimits{};
        }

        [[nodiscard]] Result<PCGGraphSourceLimits> ResolveLimits(const PCGGraphSourceContext &context) {
            auto tierLimits = LimitsForTier(context.tier);
            if (tierLimits.HasError())
                return Failed<PCGGraphSourceLimits>(PCGErrors::GraphSourceCapacityExceeded);
            if (IsDefaultLimits(context.limits))
                return GraphSourceLimitsForTier(context.tier);
            if (!IsValidLimits(context.limits, tierLimits.Value()))
                return Failed<PCGGraphSourceLimits>(PCGErrors::GraphSourceCapacityExceeded);
            return Result<PCGGraphSourceLimits>::Success(context.limits);
        }

        [[nodiscard]] Result<void> ValidateAdmission(const PCGGraphSourceContext &context) {
            if (!IsKnown(context.admission) || !IsKnown(context.unknownNodePolicy))
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceMalformed));
            if (context.admission != PCGGraphSourceAdmissionState::Accepting)
                return Result<void>::Failure(Failure(PCGErrors::GraphLifecycleUnavailable));
            return Result<void>::Success();
        }

        class ByteReader final {
        public:
            explicit ByteReader(const std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

            template <typename Integer> [[nodiscard]] std::optional<Integer> ReadInteger() {
                static_assert(std::is_integral_v<Integer>);
                if (Remaining() < sizeof(Integer))
                    return std::nullopt;
                using Unsigned = std::make_unsigned_t<Integer>;
                Unsigned value{};
                for (std::size_t index = 0; index < sizeof(Integer); ++index)
                    value = static_cast<Unsigned>((value << 8U) | bytes_[offset_++]);
                return static_cast<Integer>(value);
            }

            [[nodiscard]] std::optional<std::span<const std::uint8_t>> ReadBytes(const std::size_t count) {
                if (Remaining() < count)
                    return std::nullopt;
                const auto result = bytes_.subspan(offset_, count);
                offset_ += count;
                return result;
            }

            [[nodiscard]] std::optional<std::string> ReadString(const std::size_t maximum) {
                const auto size = ReadInteger<std::uint16_t>();
                if (!size.has_value() || *size > maximum)
                    return std::nullopt;
                const auto bytes = ReadBytes(*size);
                if (!bytes.has_value())
                    return std::nullopt;
                return std::string{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
            }

            [[nodiscard]] std::size_t Remaining() const noexcept {
                return bytes_.size() - offset_;
            }

        private:
            std::span<const std::uint8_t> bytes_;
            std::size_t offset_{};
        };

        template <typename Identity> [[nodiscard]] std::optional<Identity> ReadIdentity(ByteReader &reader) {
            const auto value = reader.ReadInteger<std::uint64_t>();
            if (!value.has_value())
                return std::nullopt;
            auto identity = Identity::Create(*value);
            return identity.HasValue() ? std::optional<Identity>{identity.Value()} : std::nullopt;
        }

        [[nodiscard]] std::optional<float> ReadBinary32(ByteReader &reader) {
            const auto bits = reader.ReadInteger<std::uint32_t>();
            return bits.has_value() ? std::optional<float>{std::bit_cast<float>(*bits)} : std::nullopt;
        }

        [[nodiscard]] std::optional<double> ReadBinary64(ByteReader &reader) {
            const auto bits = reader.ReadInteger<std::uint64_t>();
            return bits.has_value() ? std::optional<double>{std::bit_cast<double>(*bits)} : std::nullopt;
        }

        [[nodiscard]] std::optional<PCGGraphValue> ReadValue(ByteReader &reader) {
            const auto tag = reader.ReadInteger<std::uint8_t>();
            if (!tag.has_value())
                return std::nullopt;
            switch (*tag) {
                case 1: {
                    const auto value = reader.ReadInteger<std::uint8_t>();
                    return value.has_value() && *value <= 1 ? std::optional<PCGGraphValue>{*value != 0} : std::nullopt;
                }
                case 2: {
                    const auto value = reader.ReadInteger<std::int64_t>();
                    return value.has_value() ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 3: {
                    const auto value = reader.ReadInteger<std::uint64_t>();
                    return value.has_value() ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 4: {
                    const auto value = ReadBinary64(reader);
                    return value.has_value() ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 5: {
                    const auto x = ReadBinary32(reader);
                    const auto y = ReadBinary32(reader);
                    return x.has_value() && y.has_value() ? std::optional<PCGGraphValue>{Math::Vec2{*x, *y}} : std::nullopt;
                }
                case 6: {
                    const auto x = ReadBinary32(reader);
                    const auto y = ReadBinary32(reader);
                    const auto z = ReadBinary32(reader);
                    return x.has_value() && y.has_value() && z.has_value() ? std::optional<PCGGraphValue>{Math::Vec3{*x, *y, *z}}
                                                                           : std::nullopt;
                }
                case 7: {
                    const auto x = ReadBinary32(reader);
                    const auto y = ReadBinary32(reader);
                    const auto z = ReadBinary32(reader);
                    const auto w = ReadBinary32(reader);
                    return x.has_value() && y.has_value() && z.has_value() && w.has_value()
                               ? std::optional<PCGGraphValue>{Math::Vec4{*x, *y, *z, *w}}
                               : std::nullopt;
                }
                default:
                    return std::nullopt;
            }
        }

        [[nodiscard]] Result<PCGGraphPin> ReadPin(ByteReader &reader) {
            const auto id = ReadIdentity<PinId>(reader);
            const auto direction = reader.ReadInteger<std::uint8_t>();
            const auto type = reader.ReadInteger<std::uint8_t>();
            const auto cardinality = reader.ReadInteger<std::uint8_t>();
            const auto hasDefault = reader.ReadInteger<std::uint8_t>();
            if (!id.has_value() || !direction.has_value() || !type.has_value() || !cardinality.has_value() || !hasDefault.has_value() ||
                *hasDefault > 1)
                return Failed<PCGGraphPin>(PCGErrors::GraphSourceMalformed);

            PCGGraphPin pin{*id, static_cast<PCGPinDirection>(*direction), static_cast<PCGPinType>(*type),
                            static_cast<PCGPinCardinality>(*cardinality)};
            if (*hasDefault != 0) {
                auto value = ReadValue(reader);
                if (!value.has_value())
                    return Failed<PCGGraphPin>(PCGErrors::GraphSourceMalformed);
                pin.defaultValue = std::move(*value);
            }
            return Result<PCGGraphPin>::Success(std::move(pin));
        }

        [[nodiscard]] Result<PCGGraphNode> ReadNode(ByteReader &reader, const PCGGraphSourceLimits &limits, std::size_t &totalPins) {
            const auto id = ReadIdentity<NodeId>(reader);
            const auto type = ReadIdentity<NodeTypeId>(reader);
            const auto typeMajor = reader.ReadInteger<std::uint16_t>();
            const auto typeMinor = reader.ReadInteger<std::uint16_t>();
            const auto pinCount = reader.ReadInteger<std::uint16_t>();
            const auto payloadSize = reader.ReadInteger<std::uint32_t>();
            if (!id.has_value() || !type.has_value() || !typeMajor.has_value() || !typeMinor.has_value() || !pinCount.has_value() ||
                !payloadSize.has_value())
                return Failed<PCGGraphNode>(PCGErrors::GraphSourceMalformed);
            if (*pinCount > limits.maximumPinsPerNode ||
                *pinCount > limits.maximumTotalPins - std::min(totalPins, limits.maximumTotalPins) ||
                *payloadSize > limits.maximumNodePayloadBytes)
                return Failed<PCGGraphNode>(PCGErrors::GraphSourceCapacityExceeded);

            PCGGraphNode node{*id, *type, {*typeMajor, *typeMinor}};
            node.pins.reserve(*pinCount);
            for (std::uint16_t pinIndex = 0; pinIndex < *pinCount; ++pinIndex) {
                auto pin = ReadPin(reader);
                if (pin.HasError())
                    return Result<PCGGraphNode>::Failure(pin.ErrorValue());
                node.pins.push_back(std::move(pin).Value());
            }
            const auto payload = reader.ReadBytes(*payloadSize);
            if (!payload.has_value())
                return Failed<PCGGraphNode>(PCGErrors::GraphSourceMalformed);
            node.payload.assign(payload->begin(), payload->end());
            totalPins += *pinCount;
            return Result<PCGGraphNode>::Success(std::move(node));
        }

        [[nodiscard]] Result<PCGGraphEdge> ReadEdge(ByteReader &reader) {
            const auto id = ReadIdentity<EdgeId>(reader);
            const auto sourceNode = ReadIdentity<NodeId>(reader);
            const auto sourcePin = ReadIdentity<PinId>(reader);
            const auto targetNode = ReadIdentity<NodeId>(reader);
            const auto targetPin = ReadIdentity<PinId>(reader);
            if (!id.has_value() || !sourceNode.has_value() || !sourcePin.has_value() || !targetNode.has_value() || !targetPin.has_value())
                return Failed<PCGGraphEdge>(PCGErrors::GraphSourceMalformed);
            return Result<PCGGraphEdge>::Success({*id, *sourceNode, *sourcePin, *targetNode, *targetPin});
        }

        [[nodiscard]] Result<PCGExposedInput> ReadExposedInput(ByteReader &reader, const std::size_t maximumIdentifierBytes) {
            const auto id = ReadIdentity<ExposedInputId>(reader);
            auto key = reader.ReadString(maximumIdentifierBytes);
            const auto node = ReadIdentity<NodeId>(reader);
            const auto pin = ReadIdentity<PinId>(reader);
            auto value = ReadValue(reader);
            if (!id.has_value() || !key.has_value() || !node.has_value() || !pin.has_value() || !value.has_value())
                return Failed<PCGExposedInput>(PCGErrors::GraphSourceMalformed);
            return Result<PCGExposedInput>::Success({*id, std::move(*key), *node, *pin, std::move(*value)});
        }

        [[nodiscard]] Result<PCGGraphAsset> DecodeCurrent(const std::span<const std::uint8_t> source, const PCGGraphSourceContext &context,
                                                          const PCGGraphSourceLimits &limits) {
            ByteReader reader{source};
            const auto magic = reader.ReadBytes(GraphMagic.size());
            const auto major = reader.ReadInteger<std::uint16_t>();
            const auto minor = reader.ReadInteger<std::uint16_t>();
            const auto graph = ReadIdentity<GraphId>(reader);
            const auto revision = ReadIdentity<GraphRevision>(reader);
            const auto tier = reader.ReadInteger<std::uint8_t>();
            const auto mode = reader.ReadInteger<std::uint8_t>();
            const auto seed = reader.ReadInteger<std::uint64_t>();
            const auto nodeCount = reader.ReadInteger<std::uint32_t>();
            const auto edgeCount = reader.ReadInteger<std::uint32_t>();
            const auto inputCount = reader.ReadInteger<std::uint32_t>();
            if (!magic.has_value() || !std::ranges::equal(*magic, GraphMagic) || !major.has_value() || !minor.has_value() ||
                !graph.has_value() || !revision.has_value() || !tier.has_value() || !mode.has_value() || !seed.has_value() ||
                !nodeCount.has_value() || !edgeCount.has_value() || !inputCount.has_value())
                return Failed<PCGGraphAsset>(PCGErrors::GraphSourceMalformed);
            if (*nodeCount > limits.maximumNodes || *edgeCount > limits.maximumEdges || *inputCount > limits.maximumExposedInputs)
                return Failed<PCGGraphAsset>(PCGErrors::GraphSourceCapacityExceeded);

            PCGGraphSourceData data{{*major, *minor},
                                    {*graph, *revision},
                                    static_cast<PCGOperationalTier>(*tier),
                                    static_cast<PCGGenerationMode>(*mode),
                                    *seed};
            data.nodes.reserve(*nodeCount);
            std::size_t totalPins{};
            for (std::uint32_t nodeIndex = 0; nodeIndex < *nodeCount; ++nodeIndex) {
                auto node = ReadNode(reader, limits, totalPins);
                if (node.HasError())
                    return Result<PCGGraphAsset>::Failure(node.ErrorValue());
                data.nodes.push_back(std::move(node).Value());
            }
            data.edges.reserve(*edgeCount);
            for (std::uint32_t index = 0; index < *edgeCount; ++index) {
                auto edge = ReadEdge(reader);
                if (edge.HasError())
                    return Result<PCGGraphAsset>::Failure(edge.ErrorValue());
                data.edges.push_back(std::move(edge).Value());
            }
            data.exposedInputs.reserve(*inputCount);
            for (std::uint32_t index = 0; index < *inputCount; ++index) {
                auto input = ReadExposedInput(reader, limits.maximumIdentifierBytes);
                if (input.HasError())
                    return Result<PCGGraphAsset>::Failure(input.ErrorValue());
                data.exposedInputs.push_back(std::move(input).Value());
            }
            if (reader.Remaining() != 0)
                return Failed<PCGGraphAsset>(PCGErrors::GraphSourceMalformed);
            return PCGGraphAsset::Create(std::move(data), context);
        }
    }  // namespace

    /** @copydoc GraphSourceLimitsForTier */
    Result<PCGGraphSourceLimits> GraphSourceLimitsForTier(const PCGOperationalTier tier) {
        auto limits = LimitsForTier(tier);
        if (limits.HasError())
            return Failed<PCGGraphSourceLimits>(PCGErrors::GraphSourceCapacityExceeded);
        const PCGTierLimits &tierLimits = limits.Value();
        return Result<PCGGraphSourceLimits>::Success(
            {tierLimits.maximumPlanAndAuxiliaryBytes, tierLimits.maximumNodes, tierLimits.maximumEdges, tierLimits.maximumExposedInputs,
             PCGGraphSourceHardLimits::IdentifierBytes, PCGGraphSourceHardLimits::PinsPerNode,
             std::min(PCGGraphSourceHardLimits::TotalPins, tierLimits.maximumNodes * PCGGraphSourceHardLimits::PinsPerNode),
             PCGGraphSourceHardLimits::NodePayloadBytes});
    }

    PCGGraphAsset::PCGGraphAsset(PCGGraphSourceData data, const std::size_t unknownNodeCount) noexcept
        : data_(std::move(data)), unknownNodeCount_(unknownNodeCount) {}

    /** @copydoc PCGGraphAsset::Create */
    Result<PCGGraphAsset> PCGGraphAsset::Create(PCGGraphSourceData candidate, const PCGGraphSourceContext &context) {
        if (auto admission = ValidateAdmission(context); admission.HasError())
            return Result<PCGGraphAsset>::Failure(admission.ErrorValue());
        auto limits = ResolveLimits(context);
        if (limits.HasError())
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceCapacityExceeded);
        if (ClassifyPCGGraphSchemaCompatibility(candidate.version) != PCGGraphSchemaCompatibility::Exact)
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceVersionUnsupported);
        if (!candidate.generation.IsValid() || !IsKnown(candidate.tier) || candidate.tier != context.tier || !IsKnown(candidate.mode) ||
            (candidate.tier == PCGOperationalTier::Baseline && candidate.mode != PCGGenerationMode::Offline))
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceMalformed);

        auto validation = Detail::ValidateAndCanonicalizeGraph(candidate, context, limits.Value());
        if (validation.HasError())
            return Result<PCGGraphAsset>::Failure(validation.ErrorValue());
        PCGGraphAsset asset{std::move(candidate), validation.Value()};
        return Result<PCGGraphAsset>::Success(std::move(asset));
    }

    /** @copydoc PCGGraphAsset::Data */
    const PCGGraphSourceData &PCGGraphAsset::Data() const noexcept {
        return data_;
    }

    /** @copydoc PCGGraphAsset::UnknownNodeCount */
    std::size_t PCGGraphAsset::UnknownNodeCount() const noexcept {
        return unknownNodeCount_;
    }

    /** @copydoc PCGGraphAsset::IsCookEligible */
    bool PCGGraphAsset::IsCookEligible() const noexcept {
        return unknownNodeCount_ == 0;
    }

    /** @copydoc InspectPCGGraphSchemaVersion */
    Result<PCGGraphSchemaVersion> InspectPCGGraphSchemaVersion(const std::span<const std::uint8_t> source) {
        if (source.size() < 8 || !std::ranges::equal(source.first<4>(), GraphMagic))
            return Failed<PCGGraphSchemaVersion>(PCGErrors::GraphSourceMalformed);
        ByteReader reader{source.subspan(4)};
        const auto major = reader.ReadInteger<std::uint16_t>();
        const auto minor = reader.ReadInteger<std::uint16_t>();
        return major.has_value() && minor.has_value() ? Result<PCGGraphSchemaVersion>::Success({*major, *minor})
                                                      : Failed<PCGGraphSchemaVersion>(PCGErrors::GraphSourceMalformed);
    }

    /** @copydoc DeserializePCGGraphAsset */
    Result<PCGGraphAsset> DeserializePCGGraphAsset(const std::span<const std::uint8_t> source, const PCGGraphSourceContext &context,
                                                   const IPCGGraphSourceMigrator *migrator) {
        if (auto admission = ValidateAdmission(context); admission.HasError())
            return Result<PCGGraphAsset>::Failure(admission.ErrorValue());
        auto limits = ResolveLimits(context);
        if (limits.HasError() || source.size() > (limits.HasValue() ? limits.Value().maximumSourceBytes : 0))
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceCapacityExceeded);
        auto version = InspectPCGGraphSchemaVersion(source);
        if (version.HasError())
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceMalformed);
        using enum PCGGraphSchemaCompatibility;
        switch (ClassifyPCGGraphSchemaCompatibility(version.Value())) {
            case Exact:
                return DecodeCurrent(source, context, limits.Value());
            case MigrationRequired: {
                if (migrator == nullptr)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                auto migrated = migrator->Migrate(source, version.Value(), CurrentPCGGraphSchemaVersion, limits.Value().maximumSourceBytes);
                if (migrated.HasError() || migrated.Value().size() > limits.Value().maximumSourceBytes)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                if (auto migratedVersion = InspectPCGGraphSchemaVersion(migrated.Value());
                    migratedVersion.HasError() || migratedVersion.Value() != CurrentPCGGraphSchemaVersion)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                return DecodeCurrent(migrated.Value(), context, limits.Value());
            }
            case Unsupported:
                return Failed<PCGGraphAsset>(PCGErrors::GraphSourceVersionUnsupported);
        }
        return Failed<PCGGraphAsset>(PCGErrors::GraphSourceVersionUnsupported);
    }

    /** @copydoc ReplacePCGGraphAsset */
    Result<PCGGraphAsset> ReplacePCGGraphAsset(const PCGGraphAsset &current, PCGGraphSourceData candidate,
                                               const PCGGraphSourceContext &context) {
        if (candidate.generation.graph != current.Data().generation.graph ||
            candidate.generation.revision <= current.Data().generation.revision)
            return Failed<PCGGraphAsset>(PCGErrors::GraphReplacementInvalid);
        return PCGGraphAsset::Create(std::move(candidate), context);
    }
}  // namespace Horo::PCG
