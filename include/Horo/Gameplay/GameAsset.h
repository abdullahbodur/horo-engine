#pragma once

/**
 * @file GameAsset.h
 * @brief Stable project-owned asset data, editor metadata, and processing bindings.
 */

#include "Horo/Foundation/AssetCookTargetId.h"
#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Result.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Horo::Gameplay {
    inline constexpr std::size_t MaximumGameAssetTypeIdBytes = 96;
    inline constexpr std::size_t MaximumGameAssetFieldIdBytes = 96;
    inline constexpr std::size_t MaximumGameAssetExtensionBytes = 32;
    inline constexpr std::size_t MaximumGameAssetCookTargetBytes = 96;
    inline constexpr std::size_t MaximumGameAssetEditorTextBytes = 256;
    inline constexpr std::size_t MaximumGameAssetTypes = 256;
    inline constexpr std::size_t MaximumGameAssetFields = 256;
    inline constexpr std::size_t MaximumGameAssetSourceExtensions = 32;
    inline constexpr std::size_t MaximumGameAssetCookTargets = 32;
    inline constexpr std::size_t MaximumGameAssetPayloadBytes = 64U * 1024U * 1024U;
    inline constexpr std::size_t MaximumGameAssetCookedBytes = 256U * 1024U * 1024U;

    /** @brief Stable persistent identity of one project-owned asset type. */
    class GameAssetTypeId final {
    public:
        GameAssetTypeId() = default;

        /**
         * @brief Parses a project-namespaced asset type identity.
         * @param value Identifier using the `game.<module>.<asset_type>` namespace.
         * @return Valid identity or a typed validation error.
         */
        [[nodiscard]] static Result<GameAssetTypeId> Parse(std::string_view value);
        /** @brief Returns the persistent identifier text. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const GameAssetTypeId &) const noexcept = default;

    private:
        explicit GameAssetTypeId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Stable field identity used by generic editor surfaces. */
    class GameAssetFieldId final {
    public:
        GameAssetFieldId() = default;

        /** @brief Parses a lowercase field identity. @param value Field text. @return Valid identity or a typed error. */
        [[nodiscard]] static Result<GameAssetFieldId> Parse(std::string_view value);
        /** @brief Returns the persistent field identity. */
        [[nodiscard]] const std::string &Value() const noexcept;
        /** @brief Reports whether this value contains a parsed identity. */
        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] auto operator<=>(const GameAssetFieldId &) const noexcept = default;

    private:
        explicit GameAssetFieldId(std::string value) : value_(std::move(value)) {}

        std::string value_;
    };

    /** @brief Language-neutral field kind rendered by generic asset editors. */
    enum class GameAssetFieldKind : std::uint8_t {
        Boolean,
        SignedInteger,
        Number,
        String,
        AssetReference,
        Vec2,
        Vec3,
        Quaternion,
    };

    /** @brief One declarative field in a project-owned asset editor representation. */
    struct GameAssetEditorFieldDescriptor {
        GameAssetFieldId id;                                 /**< Stable serialized field identity. */
        std::string displayName;                             /**< Project-authored user-facing field name. */
        GameAssetFieldKind kind{GameAssetFieldKind::String}; /**< Generic editor value kind. */
        bool required{false};                                /**< Whether authoring requires a value. */
    };

    /** @brief Rendering-neutral metadata used by editor and headless inspection surfaces. */
    struct GameAssetEditorRepresentation {
        std::string displayName;                            /**< Project-authored user-facing type name. */
        std::string category;                               /**< Stable editor grouping label. */
        std::string iconName;                               /**< Rendering-neutral icon role. */
        std::vector<GameAssetEditorFieldDescriptor> fields; /**< Generic field schema in display order. */
    };

    /** @brief Complete declarative contract for one project-owned asset type. */
    struct GameAssetTypeDescriptor {
        GameAssetTypeId typeId;                     /**< Stable persistent type identity. */
        std::uint32_t schemaVersion{1};             /**< Current authored payload schema. */
        std::vector<std::string> sourceExtensions;  /**< Accepted lowercase source extensions without dots. */
        std::vector<AssetCookTargetId> cookTargets; /**< Explicit supported canonical target IDs. */
        GameAssetEditorRepresentation editor;       /**< Declarative generic editor projection. */
    };

    /** @brief Encoding of opaque authored bytes retained independently from gameplay code. */
    enum class GameAssetPayloadEncoding : std::uint8_t {
        CanonicalJson,
        Binary,
    };

    /** @brief Persistent game-asset envelope independent from native C++ layout and module lifetime. */
    struct SerializedGameAsset {
        GameAssetTypeId typeId;
        std::uint32_t schemaVersion{1};
        GameAssetPayloadEncoding encoding{GameAssetPayloadEncoding::CanonicalJson};
        std::vector<std::byte> payload;
        [[nodiscard]] bool operator==(const SerializedGameAsset &) const noexcept = default;
    };

    /** @brief Borrowed source input for one project-owned import callback. */
    struct GameAssetImportInput {
        std::span<const std::byte> sourceBytes;
        std::string_view sourceExtension;
    };

    /** @brief Borrowed editor payload submitted to the project-owned serializer. */
    struct GameAssetSerializationInput {
        std::span<const std::byte> editorPayload;
        GameAssetPayloadEncoding encoding{GameAssetPayloadEncoding::CanonicalJson};
    };

    /** @brief Borrowed authored asset and explicit target submitted to the project-owned cooker. */
    struct GameAssetCookInput {
        const SerializedGameAsset &asset;
        AssetCookTargetId target;
    };

    /** @brief Exact-generation project callbacks for import, serialization, and cooking. */
    struct GameAssetHandlerBinding {
        void *userData{};
        Result<SerializedGameAsset> (*importAsset)(void *userData, const GameAssetImportInput &input,
                                                   const CancellationToken &cancellation){};
        Result<SerializedGameAsset> (*serializeAsset)(void *userData, const GameAssetSerializationInput &input,
                                                      const CancellationToken &cancellation){};
        Result<std::vector<std::byte>> (*cookAsset)(void *userData, const GameAssetCookInput &input,
                                                    const CancellationToken &cancellation){};
    };

    /** @brief One copied descriptor paired with module-owned exact-generation processing callbacks. */
    struct GameAssetTypeRegistration {
        GameAssetTypeDescriptor descriptor;
        GameAssetHandlerBinding handler;
    };

    /**
     * @brief Validates a persistent game-asset envelope without invoking project code.
     * @param asset Candidate opaque payload.
     * @return Success or a stable bounded-data error.
     */
    [[nodiscard]] Result<void> ValidateSerializedGameAsset(const SerializedGameAsset &asset);
}  // namespace Horo::Gameplay
