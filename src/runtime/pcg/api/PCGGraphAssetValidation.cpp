#include "Horo/PCG/PCGErrors.h"
#include "PCGGraphAssetInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Horo::PCG {
    namespace {
        constexpr std::size_t GraphEnvelopeBytes = 46;
        constexpr std::size_t EncodedNodeHeaderBytes = 26;
        constexpr std::size_t EncodedPinHeaderBytes = 12;
        constexpr std::size_t EncodedEdgeBytes = 40;
        constexpr std::size_t EncodedInputHeaderBytes = 26;

        template <typename T> [[nodiscard]] Result<T> Rejected(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
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

        [[nodiscard]] constexpr std::size_t EncodedValueBytes(const PCGGraphValue &value) noexcept {
            switch (value.index()) {
                case 1:
                    return 2;
                case 2:
                case 3:
                case 4:
                case 5:
                    return 9;
                case 6:
                    return 13;
                case 7:
                    return 17;
                default:
                    return 0;
            }
        }

        [[nodiscard]] bool ConsumeEncodedBytes(std::size_t &remaining, const std::size_t count) noexcept {
            if (count > remaining)
                return false;
            remaining -= count;
            return true;
        }

        [[nodiscard]] bool FitsEncodedSource(const PCGGraphSourceData &data, const std::size_t maximumBytes) noexcept {
            std::size_t remaining = maximumBytes;
            if (!ConsumeEncodedBytes(remaining, GraphEnvelopeBytes))
                return false;
            for (const PCGGraphNode &node : data.nodes) {
                if (!ConsumeEncodedBytes(remaining, EncodedNodeHeaderBytes) || !ConsumeEncodedBytes(remaining, node.payload.size()))
                    return false;
                for (const PCGGraphPin &pin : node.pins) {
                    const std::size_t defaultBytes = pin.defaultValue ? EncodedValueBytes(*pin.defaultValue) : 0;
                    if (!ConsumeEncodedBytes(remaining, EncodedPinHeaderBytes + defaultBytes))
                        return false;
                }
            }
            if (data.edges.size() > remaining / EncodedEdgeBytes || !ConsumeEncodedBytes(remaining, data.edges.size() * EncodedEdgeBytes))
                return false;
            for (const PCGExposedInput &input : data.exposedInputs) {
                const std::size_t valueBytes = EncodedValueBytes(input.defaultValue);
                if (!ConsumeEncodedBytes(remaining, EncodedInputHeaderBytes + input.key.size() + valueBytes))
                    return false;
            }
            return true;
        }

        enum class NodeSupportState : std::uint8_t {
            Supported,
            Unknown,
            VersionUnsupported
        };

        [[nodiscard]] Result<std::vector<PCGNodeTypeSupport>> PrepareNodeSupport(const std::span<const PCGNodeTypeSupport> support) {
            if (support.size() > PCGGraphSourceHardLimits::NodeTypes)
                return Rejected<std::vector<PCGNodeTypeSupport>>(PCGErrors::GraphSourceCapacityExceeded);
            std::vector<PCGNodeTypeSupport> sorted{support.begin(), support.end()};
            std::ranges::sort(sorted, {}, &PCGNodeTypeSupport::type);
            for (const PCGNodeTypeSupport &entry : sorted) {
                if (!entry.type.IsValid() || !entry.minimumVersion.IsValid() || !entry.maximumVersion.IsValid() ||
                    entry.minimumVersion > entry.maximumVersion)
                    return Rejected<std::vector<PCGNodeTypeSupport>>(PCGErrors::GraphSourceMalformed);
            }
            if (std::ranges::adjacent_find(sorted, {}, &PCGNodeTypeSupport::type) != sorted.end())
                return Rejected<std::vector<PCGNodeTypeSupport>>(PCGErrors::GraphSourceMalformed);
            return Result<std::vector<PCGNodeTypeSupport>>::Success(std::move(sorted));
        }

        [[nodiscard]] NodeSupportState FindNodeSupport(const PCGGraphNode &node, std::span<const PCGNodeTypeSupport> support) {
            const auto found = std::ranges::lower_bound(support, node.type, {}, &PCGNodeTypeSupport::type);
            if (found == support.end() || found->type != node.type)
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

        [[nodiscard]] Result<void> ValidateNodeContract(const PCGGraphNode &node, const PCGUnknownNodePolicy unknownNodePolicy,
                                                        const std::span<const PCGNodeTypeSupport> nodeSupport,
                                                        const PCGGraphSourceLimits &limits, std::size_t &unknownNodeCount) {
            if (node.pins.size() > limits.maximumPinsPerNode || node.payload.size() > limits.maximumNodePayloadBytes)
                return Rejected<void>(PCGErrors::GraphSourceCapacityExceeded);
            if (!node.id.IsValid() || !node.type.IsValid() || !node.version.IsValid())
                return Rejected<void>(PCGErrors::GraphSourceMalformed);

            const NodeSupportState support = FindNodeSupport(node, nodeSupport);
            if (support == NodeSupportState::VersionUnsupported)
                return Rejected<void>(PCGErrors::GraphSourceVersionUnsupported);
            if (support == NodeSupportState::Unknown) {
                if (unknownNodePolicy == PCGUnknownNodePolicy::Reject)
                    return Rejected<void>(PCGErrors::GraphNodeTypeUnknown);
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
                return Rejected<void>(PCGErrors::GraphSourceMalformed);
            if (pin.defaultValue)
                CanonicalizeValue(*pin.defaultValue);
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> AppendNodePins(PCGGraphNode &node, const PCGGraphSourceLimits &limits,
                                                  std::vector<PinReference> &references) {
            std::ranges::sort(node.pins, {}, &PCGGraphPin::id);
            if (std::ranges::adjacent_find(node.pins, {}, &PCGGraphPin::id) != node.pins.end())
                return Rejected<void>(PCGErrors::GraphSourceDuplicate);
            if (node.pins.size() > limits.maximumTotalPins - std::min(references.size(), limits.maximumTotalPins))
                return Rejected<void>(PCGErrors::GraphSourceCapacityExceeded);
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
                return Rejected<std::vector<PinReference>>(PCGErrors::GraphSourceCapacityExceeded);
            auto nodeSupport = PrepareNodeSupport(context.supportedNodeTypes);
            if (nodeSupport.HasError())
                return Result<std::vector<PinReference>>::Failure(nodeSupport.ErrorValue());

            std::ranges::sort(data.nodes, {}, &PCGGraphNode::id);
            if (std::ranges::adjacent_find(data.nodes, {}, &PCGGraphNode::id) != data.nodes.end())
                return Rejected<std::vector<PinReference>>(PCGErrors::GraphSourceDuplicate);
            std::vector<PinReference> references;
            references.reserve(std::min(limits.maximumTotalPins, data.nodes.size() * std::min(limits.maximumPinsPerNode, std::size_t{4})));
            for (PCGGraphNode &node : data.nodes) {
                if (auto valid = ValidateNodeContract(node, context.unknownNodePolicy, nodeSupport.Value(), limits, unknownNodeCount);
                    valid.HasError())
                    return Result<std::vector<PinReference>>::Failure(valid.ErrorValue());
                if (auto appended = AppendNodePins(node, limits, references); appended.HasError())
                    return Result<std::vector<PinReference>>::Failure(appended.ErrorValue());
            }
            std::ranges::sort(references, {}, &PinReference::pin);
            if (std::ranges::adjacent_find(references, {}, &PinReference::pin) != references.end())
                return Rejected<std::vector<PinReference>>(PCGErrors::GraphSourceDuplicate);
            return Result<std::vector<PinReference>>::Success(std::move(references));
        }

        [[nodiscard]] Result<void> ValidateEdges(PCGGraphSourceData &data, const PCGGraphSourceLimits &limits,
                                                 std::span<const PinReference> pins) {
            if (data.edges.size() > limits.maximumEdges)
                return Rejected<void>(PCGErrors::GraphSourceCapacityExceeded);
            std::ranges::sort(data.edges, {}, &PCGGraphEdge::id);
            std::vector<PinId> singleInputs;
            std::vector<std::array<std::uint64_t, 4>> endpoints;
            singleInputs.reserve(data.edges.size());
            endpoints.reserve(data.edges.size());
            for (std::size_t index = 0; index < data.edges.size(); ++index) {
                const PCGGraphEdge &edge = data.edges[index];
                if (!edge.id.IsValid() || !edge.sourceNode.IsValid() || !edge.sourcePin.IsValid() || !edge.targetNode.IsValid() ||
                    !edge.targetPin.IsValid())
                    return Rejected<void>(PCGErrors::GraphSourceMalformed);
                if (index > 0 && data.edges[index - 1].id == edge.id)
                    return Rejected<void>(PCGErrors::GraphSourceDuplicate);
                const std::array endpoint{edge.sourceNode.Value(), edge.sourcePin.Value(), edge.targetNode.Value(), edge.targetPin.Value()};
                endpoints.push_back(endpoint);
                const PinReference *source = FindPin(pins, edge.sourcePin);
                const PinReference *target = FindPin(pins, edge.targetPin);
                if (source == nullptr || target == nullptr || source->node != edge.sourceNode || target->node != edge.targetNode ||
                    source->direction != PCGPinDirection::Output || target->direction != PCGPinDirection::Input ||
                    source->type != target->type || edge.sourceNode == edge.targetNode)
                    return Rejected<void>(PCGErrors::GraphTopologyInvalid);
                if (target->cardinality == PCGPinCardinality::Single) {
                    singleInputs.push_back(edge.targetPin);
                }
            }
            std::ranges::sort(endpoints);
            if (std::ranges::adjacent_find(endpoints) != endpoints.end())
                return Rejected<void>(PCGErrors::GraphSourceDuplicate);
            std::ranges::sort(singleInputs);
            if (std::ranges::adjacent_find(singleInputs) != singleInputs.end())
                return Rejected<void>(PCGErrors::GraphTopologyInvalid);
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
                    return Rejected<void>(PCGErrors::GraphTopologyInvalid);
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
            return visited == data.nodes.size() ? Result<void>::Success() : Rejected<void>(PCGErrors::GraphTopologyInvalid);
        }

        [[nodiscard]] Result<void> ValidateExposedInputs(PCGGraphSourceData &data, const PCGGraphSourceLimits &limits,
                                                         std::span<const PinReference> pins) {
            if (data.exposedInputs.size() > limits.maximumExposedInputs)
                return Rejected<void>(PCGErrors::GraphSourceCapacityExceeded);
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
                    return Rejected<void>(PCGErrors::GraphSourceMalformed);
                if ((index > 0 && data.exposedInputs[index - 1].id == input.id) ||
                    std::ranges::find(keys, std::string_view{input.key}) != keys.end() ||
                    std::ranges::find(exposedPins, input.pin) != exposedPins.end())
                    return Rejected<void>(PCGErrors::GraphSourceDuplicate);
                CanonicalizeValue(input.defaultValue);
                keys.emplace_back(input.key);
                exposedPins.push_back(input.pin);
            }
            return Result<void>::Success();
        }

    }  // namespace

    /** @brief Validates and canonicalizes one graph candidate without publishing it. */
    Result<std::size_t> Detail::ValidateAndCanonicalizeGraph(PCGGraphSourceData &candidate, const PCGGraphSourceContext &context,
                                                             const PCGGraphSourceLimits &limits) {
        std::size_t unknownNodeCount{};
        auto pins = ValidateNodes(candidate, context, limits, unknownNodeCount);
        if (pins.HasError())
            return Result<std::size_t>::Failure(pins.ErrorValue());
        if (auto edges = ValidateEdges(candidate, limits, pins.Value()); edges.HasError())
            return Result<std::size_t>::Failure(edges.ErrorValue());
        if (auto acyclic = ValidateAcyclic(candidate); acyclic.HasError())
            return Result<std::size_t>::Failure(acyclic.ErrorValue());
        if (auto inputs = ValidateExposedInputs(candidate, limits, pins.Value()); inputs.HasError())
            return Result<std::size_t>::Failure(inputs.ErrorValue());
        if (!FitsEncodedSource(candidate, limits.maximumSourceBytes))
            return Rejected<std::size_t>(PCGErrors::GraphSourceCapacityExceeded);
        return Result<std::size_t>::Success(unknownNodeCount);
    }
}  // namespace Horo::PCG
