#include "Horo/PCG/PCGGraphAsset.h"

#include "Horo/PCG/PCGErrors.h"

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
        constexpr std::size_t FixedHeaderBytes = 46;

        [[nodiscard]] Error Failure(const ErrorCodeDescriptor &descriptor) {
            return MakeError(descriptor);
        }

        template <typename T> [[nodiscard]] Result<T> Failed(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(Failure(descriptor));
        }

        [[nodiscard]] bool IsKnown(const PCGOperationalTier value) noexcept {
            return value == PCGOperationalTier::Baseline || value == PCGOperationalTier::Standard || value == PCGOperationalTier::High;
        }

        template <typename Enum> [[nodiscard]] bool IsKnown(const Enum value) noexcept {
            return value < Enum::Count;
        }

        [[nodiscard]] bool IsLowerAscii(const unsigned char value) noexcept {
            return value >= 'a' && value <= 'z';
        }

        [[nodiscard]] bool IsIdentifierTail(const unsigned char value) noexcept {
            return IsLowerAscii(value) || (value >= '0' && value <= '9') || value == '_';
        }

        [[nodiscard]] bool IsCanonicalIdentifier(const std::string_view value, const std::size_t maximumBytes) noexcept {
            if (value.empty() || value.size() > maximumBytes || value.find('.') == std::string_view::npos)
                return false;
            std::size_t start{};
            while (start < value.size()) {
                const std::size_t separator = value.find('.', start);
                const std::size_t end = separator == std::string_view::npos ? value.size() : separator;
                const std::string_view part = value.substr(start, end - start);
                if (part.empty() || !IsLowerAscii(static_cast<unsigned char>(part.front())) ||
                    !std::ranges::all_of(part.substr(1), IsIdentifierTail))
                    return false;
                if (separator == std::string_view::npos)
                    return true;
                start = separator + 1;
            }
            return false;
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

        [[nodiscard]] PCGPinType ValueType(const PCGGraphValue &value) noexcept {
            switch (value.index()) {
                case 1:
                    return PCGPinType::Boolean;
                case 2:
                    return PCGPinType::SignedInteger;
                case 3:
                    return PCGPinType::UnsignedInteger;
                case 4:
                    return PCGPinType::Scalar;
                case 5:
                    return PCGPinType::Vector2;
                case 6:
                    return PCGPinType::Vector3;
                case 7:
                    return PCGPinType::Vector4;
                default:
                    return PCGPinType::Count;
            }
        }

        [[nodiscard]] bool IsFinite(const PCGGraphValue &value) noexcept {
            return std::visit([]<typename Value>(const Value &item) {
                using T = std::remove_cvref_t<Value>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    return false;
                if constexpr (std::is_same_v<T, double>)
                    return std::isfinite(item);
                if constexpr (std::is_same_v<T, Math::Vec2> || std::is_same_v<T, Math::Vec3> || std::is_same_v<T, Math::Vec4>)
                    return Math::IsFinite(item);
                return true;
            }, value);
        }

        void CanonicalizeScalar(double &value) noexcept {
            if (value == 0.0)
                value = 0.0;
        }

        void CanonicalizeScalar(float &value) noexcept {
            if (value == 0.0F)
                value = 0.0F;
        }

        void CanonicalizeValue(PCGGraphValue &value) noexcept {
            std::visit([]<typename Value>(Value &item) {
                using T = std::remove_cvref_t<Value>;
                if constexpr (std::is_same_v<T, double>) {
                    CanonicalizeScalar(item);
                } else if constexpr (std::is_same_v<T, Math::Vec2>) {
                    CanonicalizeScalar(item.x);
                    CanonicalizeScalar(item.y);
                } else if constexpr (std::is_same_v<T, Math::Vec3>) {
                    CanonicalizeScalar(item.x);
                    CanonicalizeScalar(item.y);
                    CanonicalizeScalar(item.z);
                } else if constexpr (std::is_same_v<T, Math::Vec4>) {
                    CanonicalizeScalar(item.x);
                    CanonicalizeScalar(item.y);
                    CanonicalizeScalar(item.z);
                    CanonicalizeScalar(item.w);
                }
            }, value);
        }

        [[nodiscard]] bool HasDuplicateNodeSupport(std::span<const PCGNodeTypeSupport> support) {
            for (std::size_t index = 0; index < support.size(); ++index) {
                if (!support[index].type.IsValid() || !support[index].minimumVersion.IsValid() ||
                    !support[index].maximumVersion.IsValid() || support[index].minimumVersion > support[index].maximumVersion)
                    return true;
                if (std::ranges::any_of(support.first(index), [&support, index](const PCGNodeTypeSupport &candidate) {
                    return candidate.type == support[index].type;
                }))
                    return true;
            }
            return false;
        }

        enum class NodeSupportState : std::uint8_t {
            Supported,
            Unknown,
            VersionUnsupported
        };

        [[nodiscard]] NodeSupportState FindNodeSupport(const PCGGraphNode &node, std::span<const PCGNodeTypeSupport> support) {
            const auto found = std::ranges::find(support, node.type, &PCGNodeTypeSupport::type);
            if (found == support.end())
                return NodeSupportState::Unknown;
            return node.version >= found->minimumVersion && node.version <= found->maximumVersion ? NodeSupportState::Supported
                                                                                                  : NodeSupportState::VersionUnsupported;
        }

        struct PinReference final {
            PinId pin{};
            NodeId node{};
            PCGPinDirection direction{PCGPinDirection::Input};
            PCGPinType type{PCGPinType::PointSet};
            PCGPinCardinality cardinality{PCGPinCardinality::Single};
        };

        [[nodiscard]] const PinReference *FindPin(std::span<const PinReference> pins, const PinId id) noexcept {
            const auto found = std::ranges::lower_bound(pins, id, {}, &PinReference::pin);
            return found == pins.end() || found->pin != id ? nullptr : &*found;
        }

        [[nodiscard]] Result<void> ValidateNodeContract(const PCGGraphNode &node, const PCGGraphSourceContext &context,
                                                        const PCGGraphSourceLimits &limits, std::size_t &unknownNodeCount) {
            if (node.pins.size() > limits.maximumPinsPerNode || node.payload.size() > limits.maximumNodePayloadBytes)
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceCapacityExceeded));
            if (!node.id.IsValid() || !node.type.IsValid() || !node.version.IsValid())
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceMalformed));

            const NodeSupportState support = FindNodeSupport(node, context.supportedNodeTypes);
            if (support == NodeSupportState::VersionUnsupported)
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceVersionUnsupported));
            if (support == NodeSupportState::Unknown) {
                if (context.unknownNodePolicy == PCGUnknownNodePolicy::Reject)
                    return Result<void>::Failure(Failure(PCGErrors::GraphNodeTypeUnknown));
                ++unknownNodeCount;
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidatePinContract(PCGGraphPin &pin) {
            const bool outputContractInvalid = pin.direction == PCGPinDirection::Output &&
                                               (pin.cardinality != PCGPinCardinality::Multiple || pin.defaultValue.has_value());
            const bool defaultInvalid =
                pin.defaultValue.has_value() &&
                (!IsFinite(*pin.defaultValue) || ValueType(*pin.defaultValue) != pin.type || pin.type == PCGPinType::PointSet);
            if (!pin.id.IsValid() || !IsKnown(pin.direction) || !IsKnown(pin.type) || !IsKnown(pin.cardinality) || outputContractInvalid ||
                defaultInvalid)
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceMalformed));
            if (pin.defaultValue)
                CanonicalizeValue(*pin.defaultValue);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AppendNodePins(PCGGraphNode &node, const PCGGraphSourceLimits &limits,
                                                  std::vector<PinReference> &references) {
            std::ranges::sort(node.pins, {}, &PCGGraphPin::id);
            if (std::ranges::adjacent_find(node.pins, {}, &PCGGraphPin::id) != node.pins.end())
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceDuplicate));
            if (node.pins.size() > limits.maximumTotalPins - std::min(references.size(), limits.maximumTotalPins))
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceCapacityExceeded));
            for (PCGGraphPin &pin : node.pins) {
                if (auto valid = ValidatePinContract(pin); valid.HasError())
                    return valid;
                references.push_back({pin.id, node.id, pin.direction, pin.type, pin.cardinality});
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<std::vector<PinReference>> ValidateNodes(PCGGraphSourceData &data, const PCGGraphSourceContext &context,
                                                                      const PCGGraphSourceLimits &limits, std::size_t &unknownNodeCount) {
            if (data.nodes.size() > limits.maximumNodes)
                return Failed<std::vector<PinReference>>(PCGErrors::GraphSourceCapacityExceeded);
            if (HasDuplicateNodeSupport(context.supportedNodeTypes))
                return Failed<std::vector<PinReference>>(PCGErrors::GraphSourceMalformed);

            std::ranges::sort(data.nodes, {}, &PCGGraphNode::id);
            if (std::ranges::adjacent_find(data.nodes, {}, &PCGGraphNode::id) != data.nodes.end())
                return Failed<std::vector<PinReference>>(PCGErrors::GraphSourceDuplicate);
            std::vector<PinReference> references;
            references.reserve(std::min(limits.maximumTotalPins, data.nodes.size() * std::min(limits.maximumPinsPerNode, std::size_t{4})));
            for (PCGGraphNode &node : data.nodes) {
                if (auto valid = ValidateNodeContract(node, context, limits, unknownNodeCount); valid.HasError())
                    return Result<std::vector<PinReference>>::Failure(valid.ErrorValue());
                if (auto appended = AppendNodePins(node, limits, references); appended.HasError())
                    return Result<std::vector<PinReference>>::Failure(appended.ErrorValue());
            }
            std::ranges::sort(references, {}, &PinReference::pin);
            if (std::ranges::adjacent_find(references, {}, &PinReference::pin) != references.end())
                return Failed<std::vector<PinReference>>(PCGErrors::GraphSourceDuplicate);
            return Result<std::vector<PinReference>>::Success(std::move(references));
        }

        [[nodiscard]] Result<void> ValidateEdges(PCGGraphSourceData &data, const PCGGraphSourceLimits &limits,
                                                 std::span<const PinReference> pins) {
            if (data.edges.size() > limits.maximumEdges)
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceCapacityExceeded));
            std::ranges::sort(data.edges, {}, &PCGGraphEdge::id);
            std::vector<PinId> singleInputs;
            std::vector<std::array<std::uint64_t, 4>> endpoints;
            singleInputs.reserve(data.edges.size());
            endpoints.reserve(data.edges.size());
            for (std::size_t index = 0; index < data.edges.size(); ++index) {
                const PCGGraphEdge &edge = data.edges[index];
                if (!edge.id.IsValid() || !edge.sourceNode.IsValid() || !edge.sourcePin.IsValid() || !edge.targetNode.IsValid() ||
                    !edge.targetPin.IsValid())
                    return Result<void>::Failure(Failure(PCGErrors::GraphSourceMalformed));
                if (index > 0 && data.edges[index - 1].id == edge.id)
                    return Result<void>::Failure(Failure(PCGErrors::GraphSourceDuplicate));
                const std::array endpoint{edge.sourceNode.Value(), edge.sourcePin.Value(), edge.targetNode.Value(), edge.targetPin.Value()};
                if (std::ranges::find(endpoints, endpoint) != endpoints.end())
                    return Result<void>::Failure(Failure(PCGErrors::GraphSourceDuplicate));
                endpoints.push_back(endpoint);
                const PinReference *source = FindPin(pins, edge.sourcePin);
                const PinReference *target = FindPin(pins, edge.targetPin);
                if (source == nullptr || target == nullptr || source->node != edge.sourceNode || target->node != edge.targetNode ||
                    source->direction != PCGPinDirection::Output || target->direction != PCGPinDirection::Input ||
                    source->type != target->type || edge.sourceNode == edge.targetNode)
                    return Result<void>::Failure(Failure(PCGErrors::GraphTopologyInvalid));
                if (target->cardinality == PCGPinCardinality::Single) {
                    if (std::ranges::find(singleInputs, edge.targetPin) != singleInputs.end())
                        return Result<void>::Failure(Failure(PCGErrors::GraphTopologyInvalid));
                    singleInputs.push_back(edge.targetPin);
                }
            }
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateAcyclic(const PCGGraphSourceData &data) {
            std::vector<std::size_t> indegree(data.nodes.size());
            std::vector<std::vector<std::size_t>> outgoing(data.nodes.size());
            for (const PCGGraphEdge &edge : data.edges) {
                const auto source = std::ranges::lower_bound(data.nodes, edge.sourceNode, {}, &PCGGraphNode::id);
                const auto target = std::ranges::lower_bound(data.nodes, edge.targetNode, {}, &PCGGraphNode::id);
                if (source == data.nodes.end() || target == data.nodes.end() || source->id != edge.sourceNode ||
                    target->id != edge.targetNode)
                    return Result<void>::Failure(Failure(PCGErrors::GraphTopologyInvalid));
                const std::size_t sourceIndex = static_cast<std::size_t>(std::distance(data.nodes.begin(), source));
                const std::size_t targetIndex = static_cast<std::size_t>(std::distance(data.nodes.begin(), target));
                outgoing[sourceIndex].push_back(targetIndex);
                ++indegree[targetIndex];
            }
            std::vector<std::size_t> ready;
            ready.reserve(data.nodes.size());
            for (std::size_t index = 0; index < indegree.size(); ++index) {
                if (indegree[index] == 0)
                    ready.push_back(index);
            }
            std::size_t visited{};
            while (!ready.empty()) {
                const std::size_t node = ready.front();
                ready.erase(ready.begin());
                ++visited;
                for (const std::size_t target : outgoing[node]) {
                    if (--indegree[target] == 0) {
                        const auto insertion = std::ranges::lower_bound(ready, data.nodes[target].id, {}, [&data](const std::size_t value) {
                            return data.nodes[value].id;
                        });
                        ready.insert(insertion, target);
                    }
                }
            }
            return visited == data.nodes.size() ? Result<void>::Success() : Result<void>::Failure(Failure(PCGErrors::GraphTopologyInvalid));
        }

        [[nodiscard]] Result<void> ValidateExposedInputs(PCGGraphSourceData &data, const PCGGraphSourceLimits &limits,
                                                         std::span<const PinReference> pins) {
            if (data.exposedInputs.size() > limits.maximumExposedInputs)
                return Result<void>::Failure(Failure(PCGErrors::GraphSourceCapacityExceeded));
            std::ranges::sort(data.exposedInputs, {}, &PCGExposedInput::id);
            std::vector<std::string_view> keys;
            std::vector<PinId> exposedPins;
            keys.reserve(data.exposedInputs.size());
            exposedPins.reserve(data.exposedInputs.size());
            for (std::size_t index = 0; index < data.exposedInputs.size(); ++index) {
                PCGExposedInput &input = data.exposedInputs[index];
                const PinReference *pin = FindPin(pins, input.pin);
                if (!input.id.IsValid() || !input.node.IsValid() || !input.pin.IsValid() ||
                    !IsCanonicalIdentifier(input.key, limits.maximumIdentifierBytes) || !IsFinite(input.defaultValue) || pin == nullptr ||
                    pin->node != input.node || pin->direction != PCGPinDirection::Input || pin->type == PCGPinType::PointSet ||
                    ValueType(input.defaultValue) != pin->type)
                    return Result<void>::Failure(Failure(PCGErrors::GraphSourceMalformed));
                if ((index > 0 && data.exposedInputs[index - 1].id == input.id) ||
                    std::ranges::find(keys, std::string_view{input.key}) != keys.end() ||
                    std::ranges::find(exposedPins, input.pin) != exposedPins.end())
                    return Result<void>::Failure(Failure(PCGErrors::GraphSourceDuplicate));
                CanonicalizeValue(input.defaultValue);
                keys.emplace_back(input.key);
                exposedPins.push_back(input.pin);
            }
            return Result<void>::Success();
        }

        class ByteWriter final {
        public:
            explicit ByteWriter(const std::size_t maximum) : maximum_(maximum) {
                bytes_.reserve(std::min(maximum, std::size_t{4096}));
            }

            template <typename Integer> bool WriteInteger(const Integer value) {
                static_assert(std::is_integral_v<Integer>);
                if (!CanWrite(sizeof(Integer)))
                    return false;
                using Unsigned = std::make_unsigned_t<Integer>;
                Unsigned remaining = static_cast<Unsigned>(value);
                for (std::size_t shift = sizeof(Integer); shift > 0; --shift)
                    bytes_.push_back(static_cast<std::uint8_t>(remaining >> ((shift - 1U) * 8U)));
                return true;
            }

            bool WriteBytes(const std::span<const std::uint8_t> values) {
                if (!CanWrite(values.size()))
                    return false;
                bytes_.insert(bytes_.end(), values.begin(), values.end());
                return true;
            }

            bool WriteString(const std::string_view value) {
                return value.size() <= std::numeric_limits<std::uint16_t>::max() &&
                       WriteInteger(static_cast<std::uint16_t>(value.size())) &&
                       WriteBytes({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
            }

            [[nodiscard]] std::vector<std::uint8_t> Take() && {
                return std::move(bytes_);
            }

        private:
            [[nodiscard]] bool CanWrite(const std::size_t count) const noexcept {
                return count <= maximum_ - std::min(maximum_, bytes_.size());
            }

            std::size_t maximum_{};
            std::vector<std::uint8_t> bytes_;
        };

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
                if (!size || *size > maximum)
                    return std::nullopt;
                const auto bytes = ReadBytes(*size);
                if (!bytes)
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

        template <typename Identity> bool WriteIdentity(ByteWriter &writer, const Identity id) {
            return writer.WriteInteger(id.Value());
        }

        template <typename Identity> [[nodiscard]] std::optional<Identity> ReadIdentity(ByteReader &reader) {
            const auto value = reader.ReadInteger<std::uint64_t>();
            if (!value)
                return std::nullopt;
            auto identity = Identity::Create(*value);
            return identity.HasValue() ? std::optional<Identity>{identity.Value()} : std::nullopt;
        }

        template <typename Float> bool WriteFloat(ByteWriter &writer, const Float value) {
            using Integer = std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
            return writer.WriteInteger(std::bit_cast<Integer>(value));
        }

        template <typename Float> [[nodiscard]] std::optional<Float> ReadFloat(ByteReader &reader) {
            using Integer = std::conditional_t<sizeof(Float) == 4, std::uint32_t, std::uint64_t>;
            const auto bits = reader.ReadInteger<Integer>();
            return bits ? std::optional<Float>{std::bit_cast<Float>(*bits)} : std::nullopt;
        }

        bool WriteValue(ByteWriter &writer, const PCGGraphValue &value) {
            if (!writer.WriteInteger(static_cast<std::uint8_t>(value.index())))
                return false;
            return std::visit([&writer]<typename Value>(const Value &item) {
                using T = std::remove_cvref_t<Value>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    return false;
                } else if constexpr (std::is_same_v<T, bool>) {
                    return writer.WriteInteger(static_cast<std::uint8_t>(item));
                } else if constexpr (std::is_integral_v<T>) {
                    return writer.WriteInteger(item);
                } else if constexpr (std::is_same_v<T, double>) {
                    return WriteFloat(writer, item);
                } else if constexpr (std::is_same_v<T, Math::Vec2>) {
                    return WriteFloat(writer, item.x) && WriteFloat(writer, item.y);
                } else if constexpr (std::is_same_v<T, Math::Vec3>) {
                    return WriteFloat(writer, item.x) && WriteFloat(writer, item.y) && WriteFloat(writer, item.z);
                } else if constexpr (std::is_same_v<T, Math::Vec4>) {
                    return WriteFloat(writer, item.x) && WriteFloat(writer, item.y) && WriteFloat(writer, item.z) &&
                           WriteFloat(writer, item.w);
                } else {
                    return false;
                }
            }, value);
        }

        [[nodiscard]] std::optional<PCGGraphValue> ReadValue(ByteReader &reader) {
            const auto tag = reader.ReadInteger<std::uint8_t>();
            if (!tag)
                return std::nullopt;
            switch (*tag) {
                case 1: {
                    const auto value = reader.ReadInteger<std::uint8_t>();
                    return value && *value <= 1 ? std::optional<PCGGraphValue>{*value != 0} : std::nullopt;
                }
                case 2: {
                    const auto value = reader.ReadInteger<std::int64_t>();
                    return value ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 3: {
                    const auto value = reader.ReadInteger<std::uint64_t>();
                    return value ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 4: {
                    const auto value = ReadFloat<double>(reader);
                    return value ? std::optional<PCGGraphValue>{*value} : std::nullopt;
                }
                case 5: {
                    const auto x = ReadFloat<float>(reader);
                    const auto y = ReadFloat<float>(reader);
                    return x && y ? std::optional<PCGGraphValue>{Math::Vec2{*x, *y}} : std::nullopt;
                }
                case 6: {
                    const auto x = ReadFloat<float>(reader);
                    const auto y = ReadFloat<float>(reader);
                    const auto z = ReadFloat<float>(reader);
                    return x && y && z ? std::optional<PCGGraphValue>{Math::Vec3{*x, *y, *z}} : std::nullopt;
                }
                case 7: {
                    const auto x = ReadFloat<float>(reader);
                    const auto y = ReadFloat<float>(reader);
                    const auto z = ReadFloat<float>(reader);
                    const auto w = ReadFloat<float>(reader);
                    return x && y && z && w ? std::optional<PCGGraphValue>{Math::Vec4{*x, *y, *z, *w}} : std::nullopt;
                }
                default:
                    return std::nullopt;
            }
        }

        bool WritePin(ByteWriter &writer, const PCGGraphPin &pin) {
            const bool headerWritten = WriteIdentity(writer, pin.id) && writer.WriteInteger(static_cast<std::uint8_t>(pin.direction)) &&
                                       writer.WriteInteger(static_cast<std::uint8_t>(pin.type)) &&
                                       writer.WriteInteger(static_cast<std::uint8_t>(pin.cardinality)) &&
                                       writer.WriteInteger(static_cast<std::uint8_t>(pin.defaultValue.has_value()));
            return headerWritten && (!pin.defaultValue || WriteValue(writer, *pin.defaultValue));
        }

        bool WriteNode(ByteWriter &writer, const PCGGraphNode &node) {
            bool written = WriteIdentity(writer, node.id) && WriteIdentity(writer, node.type) && writer.WriteInteger(node.version.major) &&
                           writer.WriteInteger(node.version.minor) && writer.WriteInteger(static_cast<std::uint16_t>(node.pins.size())) &&
                           writer.WriteInteger(static_cast<std::uint32_t>(node.payload.size()));
            for (const PCGGraphPin &pin : node.pins)
                written = written && WritePin(writer, pin);
            return written && writer.WriteBytes(node.payload);
        }

        bool WriteEdge(ByteWriter &writer, const PCGGraphEdge &edge) {
            return WriteIdentity(writer, edge.id) && WriteIdentity(writer, edge.sourceNode) && WriteIdentity(writer, edge.sourcePin) &&
                   WriteIdentity(writer, edge.targetNode) && WriteIdentity(writer, edge.targetPin);
        }

        bool WriteExposedInput(ByteWriter &writer, const PCGExposedInput &input) {
            return WriteIdentity(writer, input.id) && writer.WriteString(input.key) && WriteIdentity(writer, input.node) &&
                   WriteIdentity(writer, input.pin) && WriteValue(writer, input.defaultValue);
        }

        [[nodiscard]] Result<PCGGraphPin> ReadPin(ByteReader &reader) {
            const auto id = ReadIdentity<PinId>(reader);
            const auto direction = reader.ReadInteger<std::uint8_t>();
            const auto type = reader.ReadInteger<std::uint8_t>();
            const auto cardinality = reader.ReadInteger<std::uint8_t>();
            const auto hasDefault = reader.ReadInteger<std::uint8_t>();
            if (!id || !direction || !type || !cardinality || !hasDefault || *hasDefault > 1)
                return Failed<PCGGraphPin>(PCGErrors::GraphSourceMalformed);

            PCGGraphPin pin{*id, static_cast<PCGPinDirection>(*direction), static_cast<PCGPinType>(*type),
                            static_cast<PCGPinCardinality>(*cardinality)};
            if (*hasDefault != 0) {
                auto value = ReadValue(reader);
                if (!value)
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
            if (!id || !type || !typeMajor || !typeMinor || !pinCount || !payloadSize)
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
            if (!payload)
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
            if (!id || !sourceNode || !sourcePin || !targetNode || !targetPin)
                return Failed<PCGGraphEdge>(PCGErrors::GraphSourceMalformed);
            return Result<PCGGraphEdge>::Success({*id, *sourceNode, *sourcePin, *targetNode, *targetPin});
        }

        [[nodiscard]] Result<PCGExposedInput> ReadExposedInput(ByteReader &reader, const std::size_t maximumIdentifierBytes) {
            const auto id = ReadIdentity<ExposedInputId>(reader);
            auto key = reader.ReadString(maximumIdentifierBytes);
            const auto node = ReadIdentity<NodeId>(reader);
            const auto pin = ReadIdentity<PinId>(reader);
            auto value = ReadValue(reader);
            if (!id || !key || !node || !pin || !value)
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
            if (!magic || !std::ranges::equal(*magic, GraphMagic) || !major || !minor || !graph || !revision || !tier || !mode || !seed ||
                !nodeCount || !edgeCount || !inputCount)
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

        std::size_t unknownNodeCount{};
        auto pins = ValidateNodes(candidate, context, limits.Value(), unknownNodeCount);
        if (pins.HasError())
            return Result<PCGGraphAsset>::Failure(pins.ErrorValue());
        if (auto edges = ValidateEdges(candidate, limits.Value(), pins.Value()); edges.HasError())
            return Result<PCGGraphAsset>::Failure(edges.ErrorValue());
        if (auto acyclic = ValidateAcyclic(candidate); acyclic.HasError())
            return Result<PCGGraphAsset>::Failure(acyclic.ErrorValue());
        if (auto inputs = ValidateExposedInputs(candidate, limits.Value(), pins.Value()); inputs.HasError())
            return Result<PCGGraphAsset>::Failure(inputs.ErrorValue());
        PCGGraphAsset asset{std::move(candidate), unknownNodeCount};
        if (SerializePCGGraphAsset(asset, limits.Value().maximumSourceBytes).HasError())
            return Failed<PCGGraphAsset>(PCGErrors::GraphSourceCapacityExceeded);
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
        return major && minor ? Result<PCGGraphSchemaVersion>::Success({*major, *minor})
                              : Failed<PCGGraphSchemaVersion>(PCGErrors::GraphSourceMalformed);
    }

    /** @copydoc SerializePCGGraphAsset */
    Result<std::vector<std::uint8_t>> SerializePCGGraphAsset(const PCGGraphAsset &asset, const std::size_t maximumOutputBytes) {
        if (maximumOutputBytes < FixedHeaderBytes || maximumOutputBytes > PCGGraphSourceHardLimits::SourceBytes)
            return Failed<std::vector<std::uint8_t>>(PCGErrors::GraphSourceCapacityExceeded);
        const PCGGraphSourceData &data = asset.Data();
        ByteWriter writer{maximumOutputBytes};
        bool encoded = writer.WriteBytes(GraphMagic) && writer.WriteInteger(data.version.major) &&
                       writer.WriteInteger(data.version.minor) && WriteIdentity(writer, data.generation.graph) &&
                       WriteIdentity(writer, data.generation.revision) && writer.WriteInteger(static_cast<std::uint8_t>(data.tier)) &&
                       writer.WriteInteger(static_cast<std::uint8_t>(data.mode)) && writer.WriteInteger(data.deterministicSeed) &&
                       writer.WriteInteger(static_cast<std::uint32_t>(data.nodes.size())) &&
                       writer.WriteInteger(static_cast<std::uint32_t>(data.edges.size())) &&
                       writer.WriteInteger(static_cast<std::uint32_t>(data.exposedInputs.size()));
        for (const PCGGraphNode &node : data.nodes)
            encoded = encoded && WriteNode(writer, node);
        for (const PCGGraphEdge &edge : data.edges)
            encoded = encoded && WriteEdge(writer, edge);
        for (const PCGExposedInput &input : data.exposedInputs)
            encoded = encoded && WriteExposedInput(writer, input);
        return encoded ? Result<std::vector<std::uint8_t>>::Success(std::move(writer).Take())
                       : Failed<std::vector<std::uint8_t>>(PCGErrors::GraphSourceCapacityExceeded);
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
        switch (ClassifyPCGGraphSchemaCompatibility(version.Value())) {
            case PCGGraphSchemaCompatibility::Exact:
                return DecodeCurrent(source, context, limits.Value());
            case PCGGraphSchemaCompatibility::MigrationRequired: {
                if (migrator == nullptr)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                auto migrated = migrator->Migrate(source, version.Value(), CurrentPCGGraphSchemaVersion, limits.Value().maximumSourceBytes);
                if (migrated.HasError() || migrated.Value().size() > limits.Value().maximumSourceBytes)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                auto migratedVersion = InspectPCGGraphSchemaVersion(migrated.Value());
                if (migratedVersion.HasError() || migratedVersion.Value() != CurrentPCGGraphSchemaVersion)
                    return Failed<PCGGraphAsset>(PCGErrors::GraphMigrationFailed);
                return DecodeCurrent(migrated.Value(), context, limits.Value());
            }
            case PCGGraphSchemaCompatibility::Unsupported:
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
