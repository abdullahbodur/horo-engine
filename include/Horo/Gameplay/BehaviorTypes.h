#pragma once

/**
 * @file BehaviorTypes.h
 * @brief Backend-neutral persistent types and descriptor metadata for object-attached gameplay behavior.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Horo::Gameplay {
    inline constexpr std::size_t MaximumBehaviorTypeIdBytes = 160;
    inline constexpr std::size_t MaximumBehaviorFields = 128;
    inline constexpr std::size_t MaximumBehaviorFieldNameBytes = 96;

    /** @brief Stable persistent identity of one behavior implementation. */
    class BehaviorTypeId final {
    public:
        BehaviorTypeId() = default;

        /**
         * @brief Parses a namespaced behavior identity.
         * @param value Identifier using the `game.<module>.<behavior>` namespace.
         * @return Valid identity or a typed validation error.
         */
        [[nodiscard]] static Result<BehaviorTypeId> Parse(std::string_view value);

        /** @brief Returns the persistent identifier text. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const BehaviorTypeId &) const noexcept = default;

    private:
        explicit BehaviorTypeId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Stable identity of one behavior attachment within an authored scene. */
    struct BehaviorInstanceId {
        std::uint64_t value{};

        /** @brief Reports whether this identity may be persisted. */
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const BehaviorInstanceId &) const noexcept = default;
    };

    /** @brief Supported schema-checked value types for generic behavior authoring fields. */
    using BehaviorFieldValue =
        std::variant<std::monostate, bool, std::int64_t, double, std::string, Math::Vec2, Math::Vec3, Math::Quaternion>;

    /** @brief One stable serialized authoring property. */
    struct BehaviorField {
        std::string name;
        BehaviorFieldValue value;
        [[nodiscard]] bool operator==(const BehaviorField &) const noexcept = default;
    };

    /** @brief Persistent scene payload for one object-attached behavior instance. */
    struct BehaviorComponent {
        BehaviorInstanceId instanceId;
        BehaviorTypeId typeId;
        std::uint32_t schemaVersion{1};
        bool enabled{true};
        std::vector<BehaviorField> fields;
        [[nodiscard]] bool operator==(const BehaviorComponent &) const noexcept = default;
    };

    /** @brief Runtime phase in which a behavior callback participates. */
    enum class BehaviorPhase : std::uint8_t {
        PrePhysics,
        Physics,
        PostPhysics,
        Gameplay,
        Presentation,
        RenderExtraction,
    };

    /** @brief Declarative component-access contract used for schedule validation. */
    struct BehaviorComponentAccess {
        std::vector<std::string> reads;
        std::vector<std::string> writes;
        [[nodiscard]] bool operator==(const BehaviorComponentAccess &) const noexcept = default;
    };

    /** @brief Scheduling metadata for one behavior phase. */
    struct BehaviorPhaseDescriptor {
        BehaviorPhase phase{BehaviorPhase::Gameplay};
        std::string nodeId;
        BehaviorComponentAccess access;
        std::vector<std::string> after;
        std::vector<std::string> before;
    };

    /** @brief Generic editor metadata for one serialized behavior field. */
    struct BehaviorFieldDescriptor {
        std::string name;
        BehaviorFieldValue defaultValue;
    };

    /** @brief Complete language-neutral metadata for an attachable behavior type. */
    struct BehaviorDescriptor {
        BehaviorTypeId typeId;
        std::uint32_t schemaVersion{1};
        std::string displayName;
        std::string category;
        bool allowMultiple{false};
        std::vector<BehaviorFieldDescriptor> fields;
        std::vector<BehaviorPhaseDescriptor> phases;
    };

    /**
     * @brief Validates one persistent behavior payload without requiring its implementation descriptor.
     * @param component Candidate payload.
     * @return Success or a typed bounded-data error.
     */
    [[nodiscard]] Result<void> ValidateBehaviorComponent(const BehaviorComponent &component);
}  // namespace Horo::Gameplay
