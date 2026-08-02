#pragma once

#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/Result.h"

#include <cassert>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Horo
{
    /** @brief Stable dotted configuration key identity. */
    class SettingKey
    {
    public:
        explicit SettingKey(std::string value) : m_value(std::move(value)) {}
        [[nodiscard]] const std::string &Value() const noexcept { return m_value; }
        [[nodiscard]] bool operator==(const SettingKey &) const noexcept = default;
    private:
        std::string m_value;
    };

    struct SettingKeyHash { std::size_t operator()(const SettingKey &key) const noexcept { return std::hash<std::string>{}(key.Value()); } };
    enum class SettingValueType : std::uint8_t { Boolean, Integer, String };
    enum class SettingScope : std::uint8_t { Engine, User, Project, Workspace, Session, Invocation };
    enum class ReloadPolicy : std::uint8_t { Immediate, NextFrame, NextOperation, ProjectReopen, ProcessRestart };
    enum class SettingSensitivity : std::uint8_t { Public, SecretReference };
    using SettingValue = std::variant<bool, std::int64_t, std::string>;
    using ConfigurationRevision = std::uint64_t;
    enum class ConfigurationDomain : std::uint8_t { Engine, User, Project, Workspace, Session, Invocation, All };

    /** @brief Bounded committed-change notification; consumers re-query the authoritative snapshot. */
    struct ConfigurationChangedEvent
    {
        static constexpr std::string_view HoroEventTypeName = "horo.configuration.changed.v1";
        ConfigurationRevision revision = 0;
        ConfigurationDomain domain = ConfigurationDomain::All;
        std::vector<SettingKey> changedKeys;
    };

    /** @brief Schema descriptor registered by a composition root before resolution begins. */
    struct SettingDescriptor
    {
        SettingKey key;
        SettingValueType type;
        SettingValue defaultValue;
        SettingScope scope;
        ReloadPolicy reloadPolicy;
        SettingSensitivity sensitivity;
    };

    /** @brief Draft transaction formed against one immutable configuration revision. */
    struct ConfigurationDraft
    {
        ConfigurationRevision baseRevision = 0;
        std::unordered_map<SettingKey, SettingValue, SettingKeyHash> proposedValues;
    };

    /** @brief Immutable, cheap-to-copy reference to one resolved configuration revision. */
    class ConfigurationSnapshot
    {
    public:
        [[nodiscard]] ConfigurationRevision Revision() const noexcept;
        [[nodiscard]] const SettingValue &Get(const SettingKey &key) const;
        [[nodiscard]] std::optional<SettingValue> Find(const SettingKey &key) const;
        [[nodiscard]] std::string ToJson() const;
    private:
        struct Data
        {
            ConfigurationRevision revision = 0;
            std::unordered_map<SettingKey, SettingValue, SettingKeyHash> values;
        };
        friend class ConfigurationService;
        explicit ConfigurationSnapshot(std::shared_ptr<const Data> data) : m_data(std::move(data)) {}
        std::shared_ptr<const Data> m_data;
    };
    using ConfigurationSnapshotRef = ConfigurationSnapshot;

    /** @brief Mutable schema builder sealed before the configuration service is constructed. */
    class ConfigurationSchema
    {
    public:
        [[nodiscard]] Result<void> Register(const SettingDescriptor &descriptor);
        [[nodiscard]] Result<void> Seal();
    private:
        friend class ConfigurationService;
        [[nodiscard]] static bool MatchesType(const SettingValueType type, const SettingValue &value);
        [[nodiscard]] static Error ErrorFor(const ErrorCodeDescriptor &descriptor);
        bool m_sealed = false;
        std::unordered_map<SettingKey, SettingDescriptor, SettingKeyHash> m_descriptors;
    };

    /** @brief Composition-root-owned authority that atomically replaces validated configuration snapshots. */
    class ConfigurationService
    {
    public:
        explicit ConfigurationService(ConfigurationSchema schema, EngineDataBus *events = nullptr);
        [[nodiscard]] ConfigurationSnapshot Snapshot() const;

        /** @brief Checks a draft against the current revision and schema without mutating the active snapshot. */
        [[nodiscard]] Result<void> Validate(const ConfigurationDraft &draft) const;

        /** @brief Atomically commits a draft if valid and matching baseRevision. */
        [[nodiscard]] Result<void> Commit(const ConfigurationDraft &draft);

        /** @brief Parses a JSON string into a new draft against current revision and commits it. */
        [[nodiscard]] Result<void> LoadJson(const std::string &jsonString);

        /** @brief Reads a JSON file from disk into a new draft against current revision and commits it. */
        [[nodiscard]] Result<void> LoadFile(const std::string &path);

        /** @brief Writes the current snapshot to disk as JSON atomically. */
        [[nodiscard]] Result<void> SaveFile(const std::string &path) const;

    private:
        ConfigurationSchema m_schema;
        mutable std::mutex m_mutex;
        std::shared_ptr<const ConfigurationSnapshot::Data> m_active;
        EngineDataBus *m_events = nullptr; /**< Borrowed process-owned notification bus. */
    };
} // namespace Horo
