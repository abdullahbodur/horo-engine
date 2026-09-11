#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGGraphAsset.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Horo::PCG {
    namespace {
        constexpr std::array<std::uint8_t, 4> GraphMagic{'H', 'P', 'C', 'G'};
        constexpr std::size_t FixedHeaderBytes = 46;

        template <typename T> [[nodiscard]] Result<T> Failed(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
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

        template <typename Identity> bool WriteIdentity(ByteWriter &writer, const Identity id) {
            return writer.WriteInteger(id.Value());
        }

        bool WriteBinary32(ByteWriter &writer, const float value) {
            return writer.WriteInteger(std::bit_cast<std::uint32_t>(value));
        }

        bool WriteBinary64(ByteWriter &writer, const double value) {
            return writer.WriteInteger(std::bit_cast<std::uint64_t>(value));
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
                    return WriteBinary64(writer, item);
                } else if constexpr (std::is_same_v<T, Math::Vec2>) {
                    return WriteBinary32(writer, item.x) && WriteBinary32(writer, item.y);
                } else if constexpr (std::is_same_v<T, Math::Vec3>) {
                    return WriteBinary32(writer, item.x) && WriteBinary32(writer, item.y) && WriteBinary32(writer, item.z);
                } else if constexpr (std::is_same_v<T, Math::Vec4>) {
                    return WriteBinary32(writer, item.x) && WriteBinary32(writer, item.y) && WriteBinary32(writer, item.z) &&
                           WriteBinary32(writer, item.w);
                } else {
                    return false;
                }
            }, value);
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
    }  // namespace

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
}  // namespace Horo::PCG
