#pragma once

#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/Result.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
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
        [[nodiscard]] ConfigurationRevision Revision() const noexcept { return m_data->revision; }
        [[nodiscard]] const SettingValue &Get(const SettingKey &key) const
        {
            const auto found = m_data->values.find(key);
            assert(found != m_data->values.end());
            return found->second;
        }
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
        [[nodiscard]] Result<void> Register(const SettingDescriptor &descriptor)
        {
            if (m_sealed || m_descriptors.contains(descriptor.key) || !MatchesType(descriptor.type, descriptor.defaultValue)) return Result<void>::Failure(ErrorFor("configuration.schema_invalid"));
            m_descriptors.try_emplace(descriptor.key, descriptor);
            return Result<void>::Success();
        }
        [[nodiscard]] Result<void> Seal() { if (m_sealed) return Result<void>::Failure(ErrorFor("configuration.schema_sealed")); m_sealed = true; return Result<void>::Success(); }
    private:
        friend class ConfigurationService;
        [[nodiscard]] static bool MatchesType(const SettingValueType type, const SettingValue &value)
        {
            return (type == SettingValueType::Boolean && std::holds_alternative<bool>(value)) || (type == SettingValueType::Integer && std::holds_alternative<std::int64_t>(value)) || (type == SettingValueType::String && std::holds_alternative<std::string>(value));
        }
        [[nodiscard]] static Error ErrorFor(const char *code) { return Error{ErrorCode{code}, ErrorDomainId{"horo.configuration"}, ErrorSeverity::Error, code}; }
        bool m_sealed = false;
        std::unordered_map<SettingKey, SettingDescriptor, SettingKeyHash> m_descriptors;
    };

    /** @brief Composition-root-owned authority that atomically replaces validated configuration snapshots. */
    class ConfigurationService
    {
    public:
        explicit ConfigurationService(ConfigurationSchema schema, EngineDataBus *events = nullptr) : m_schema(std::move(schema)), m_events(events)
        {
            assert(m_schema.m_sealed);
            auto initial = std::make_shared<ConfigurationSnapshot::Data>();
            for (const auto &[key, descriptor] : m_schema.m_descriptors) initial->values.try_emplace(key, descriptor.defaultValue);
            m_active = std::move(initial);
        }
        [[nodiscard]] ConfigurationSnapshot Snapshot() const { std::lock_guard lock(m_mutex); return ConfigurationSnapshot(m_active); }
        /** @brief Checks a draft against the current revision and schema without mutating the active snapshot. */
        [[nodiscard]] Result<void> Validate(const ConfigurationDraft &draft) const
        {
            std::lock_guard lock(m_mutex);
            if (draft.baseRevision != m_active->revision) return Result<void>::Failure(ConfigurationSchema::ErrorFor("configuration.draft_stale"));
            for (const auto &[key, value] : draft.proposedValues)
            {
                const auto descriptor = m_schema.m_descriptors.find(key);
                if (descriptor == m_schema.m_descriptors.end() || !ConfigurationSchema::MatchesType(descriptor->second.type, value))
                    return Result<void>::Failure(ConfigurationSchema::ErrorFor("configuration.value_invalid"));
            }
            return Result<void>::Success();
        }
        [[nodiscard]] Result<void> Commit(const ConfigurationDraft &draft)
        {
            {
                std::lock_guard lock(m_mutex);
                if (draft.baseRevision != m_active->revision) return Result<void>::Failure(ConfigurationSchema::ErrorFor("configuration.draft_stale"));
                auto candidate = std::make_shared<ConfigurationSnapshot::Data>(*m_active);
                for (const auto &[key, value] : draft.proposedValues)
                {
                    const auto descriptor = m_schema.m_descriptors.find(key);
                    if (descriptor == m_schema.m_descriptors.end() || !ConfigurationSchema::MatchesType(descriptor->second.type, value)) return Result<void>::Failure(ConfigurationSchema::ErrorFor("configuration.value_invalid"));
                    candidate->values[key] = value;
                }
                candidate->revision = m_active->revision + 1;
                m_active = std::move(candidate);
            }
            if (m_events != nullptr)
            {
                ConfigurationChangedEvent event{.revision = Snapshot().Revision()};
                event.changedKeys.reserve(draft.proposedValues.size());
                for (const auto &[key, _] : draft.proposedValues) event.changedKeys.push_back(key);
                m_events->Publish(event);
            }
            return Result<void>::Success();
        }
    private:
        ConfigurationSchema m_schema;
        mutable std::mutex m_mutex;
        std::shared_ptr<const ConfigurationSnapshot::Data> m_active;
        EngineDataBus *m_events = nullptr; /**< Borrowed process-owned notification bus. */
    };
} // namespace Horo
