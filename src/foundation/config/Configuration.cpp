#include "Horo/Foundation/Configuration.h"
#include "../FoundationErrors.h"

#include <fstream>
#include <regex>
#include <sstream>

namespace Horo {
    namespace {
        [[nodiscard]] std::string EscapeJsonString(const std::string_view value) {
            std::ostringstream escaped;
            for (const unsigned char character: value) {
                switch (character) {
                    case '"':
                        escaped << "\\\"";
                        break;
                    case '\\':
                        escaped << "\\\\";
                        break;
                    case '\b':
                        escaped << "\\b";
                        break;
                    case '\f':
                        escaped << "\\f";
                        break;
                    case '\n':
                        escaped << "\\n";
                        break;
                    case '\r':
                        escaped << "\\r";
                        break;
                    case '\t':
                        escaped << "\\t";
                        break;
                    default:
                        if (character < 0x20) {
                            static constexpr char hex[] = "0123456789abcdef";
                            escaped << "\\u00" << hex[character >> 4] << hex[character & 0x0f];
                        } else {
                            escaped << static_cast<char>(character);
                        }
                }
            }
            return escaped.str();
        }

        [[nodiscard]] std::string UnescapeJsonString(const std::string_view input) {
            std::string output;
            output.reserve(input.size());
            for (std::size_t idx = 0; idx < input.size(); ++idx) {
                if (input[idx] == '\\' && idx + 1 < input.size()) {
                    const char next = input[idx + 1];
                    switch (next) {
                        case '"':
                            output.push_back('"');
                            idx++;
                            break;
                        case '\\':
                            output.push_back('\\');
                            idx++;
                            break;
                        case 'b':
                            output.push_back('\b');
                            idx++;
                            break;
                        case 'f':
                            output.push_back('\f');
                            idx++;
                            break;
                        case 'n':
                            output.push_back('\n');
                            idx++;
                            break;
                        case 'r':
                            output.push_back('\r');
                            idx++;
                            break;
                        case 't':
                            output.push_back('\t');
                            idx++;
                            break;
                        default:
                            output.push_back(next);
                            idx++;
                            break;
                    }
                } else {
                    output.push_back(input[idx]);
                }
            }
            return output;
        }
    } // namespace

    /** @copydoc ConfigurationSnapshot::Revision */
    ConfigurationRevision ConfigurationSnapshot::Revision() const noexcept {
        return m_data->revision;
    }

    /** @copydoc ConfigurationSnapshot::Get */
    const SettingValue &ConfigurationSnapshot::Get(const SettingKey &key) const {
        const auto found = m_data->values.find(key);
        assert(found != m_data->values.end());
        return found->second;
    }

    /** @copydoc ConfigurationSnapshot::Find */
    std::optional<SettingValue> ConfigurationSnapshot::Find(const SettingKey &key) const {
        const auto found = m_data->values.find(key);
        if (found == m_data->values.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    /** @copydoc ConfigurationSnapshot::ToJson */
    std::string ConfigurationSnapshot::ToJson() const {
        std::ostringstream json;
        json << "{\n";
        json << "  \"revision\": " << m_data->revision << ",\n";
        json << "  \"values\": {\n";
        bool first = true;
        for (const auto &[key, value]: m_data->values) {
            if (!first) {
                json << ",\n";
            }
            first = false;
            json << "    \"" << EscapeJsonString(key.Value()) << "\": ";
            if (std::holds_alternative<bool>(value)) {
                json << (std::get<bool>(value) ? "true" : "false");
            } else if (std::holds_alternative<std::int64_t>(value)) {
                json << std::get<std::int64_t>(value);
            } else if (std::holds_alternative<std::string>(value)) {
                json << "\"" << EscapeJsonString(std::get<std::string>(value)) << "\"";
            }
        }
        json << "\n  }\n";
        json << "}\n";
        return json.str();
    }

    /** @copydoc ConfigurationSchema::MatchesType */
    bool ConfigurationSchema::MatchesType(const SettingValueType type, const SettingValue &value) {
        return (type == SettingValueType::Boolean && std::holds_alternative<bool>(value)) ||
               (type == SettingValueType::Integer && std::holds_alternative<std::int64_t>(value)) ||
               (type == SettingValueType::String && std::holds_alternative<std::string>(value));
    }

    /** @copydoc ConfigurationSchema::ErrorFor */
    Error ConfigurationSchema::ErrorFor(const ErrorCodeDescriptor &descriptor) {
        return MakeError(descriptor);
    }

    /** @copydoc ConfigurationSchema::Register */
    Result<void> ConfigurationSchema::Register(const SettingDescriptor &descriptor) {
        if (m_sealed || m_descriptors.contains(descriptor.key) || !
            MatchesType(descriptor.type, descriptor.defaultValue)) {
            return Result<void>::Failure(ErrorFor(ConfigurationErrors::SchemaInvalid));
        }
        m_descriptors.try_emplace(descriptor.key, descriptor);
        return Result<void>::Success();
    }

    /** @copydoc ConfigurationSchema::Seal */
    Result<void> ConfigurationSchema::Seal() {
        if (m_sealed) {
            return Result<void>::Failure(ErrorFor(ConfigurationErrors::SchemaSealed));
        }
        m_sealed = true;
        return Result<void>::Success();
    }

    /** @copydoc ConfigurationService::ConfigurationService */
    ConfigurationService::ConfigurationService(ConfigurationSchema schema, EngineDataBus *events)
        : m_schema(std::move(schema)), m_events(events) {
        assert(m_schema.m_sealed);
        auto initial = std::make_shared<ConfigurationSnapshot::Data>();
        for (const auto &[key, descriptor]: m_schema.m_descriptors) {
            initial->values.try_emplace(key, descriptor.defaultValue);
        }
        m_active = std::move(initial);
    }

    /** @copydoc ConfigurationService::Snapshot */
    ConfigurationSnapshot ConfigurationService::Snapshot() const {
        std::lock_guard lock(m_mutex);
        return ConfigurationSnapshot(m_active);
    }

    /** @copydoc ConfigurationService::Validate */
    Result<void> ConfigurationService::Validate(const ConfigurationDraft &draft) const {
        std::lock_guard lock(m_mutex);
        if (draft.baseRevision != m_active->revision) {
            return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::DraftStale));
        }
        for (const auto &[key, value]: draft.proposedValues) {
            const auto descriptor = m_schema.m_descriptors.find(key);
            if (descriptor == m_schema.m_descriptors.end() ||
                !ConfigurationSchema::MatchesType(descriptor->second.type, value)) {
                return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::ValueInvalid));
            }
        }
        return Result<void>::Success();
    }

    /** @copydoc ConfigurationService::Commit */
    Result<void> ConfigurationService::Commit(const ConfigurationDraft &draft) {
        {
            std::lock_guard lock(m_mutex);
            if (draft.baseRevision != m_active->revision) {
                return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::DraftStale));
            }
            auto candidate = std::make_shared<ConfigurationSnapshot::Data>(*m_active);
            for (const auto &[key, value]: draft.proposedValues) {
                const auto descriptor = m_schema.m_descriptors.find(key);
                if (descriptor == m_schema.m_descriptors.end() ||
                    !ConfigurationSchema::MatchesType(descriptor->second.type, value)) {
                    return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::ValueInvalid));
                }
                candidate->values[key] = value;
            }
            candidate->revision = m_active->revision + 1;
            m_active = std::move(candidate);
        }
        if (m_events != nullptr) {
            ConfigurationChangedEvent event{.revision = Snapshot().Revision()};
            event.changedKeys.reserve(draft.proposedValues.size());
            for (const auto &[key, _]: draft.proposedValues) {
                event.changedKeys.push_back(key);
            }
            m_events->Publish(event);
        }
        return Result<void>::Success();
    }

    /** @copydoc ConfigurationService::LoadJson */
    Result<void> ConfigurationService::LoadJson(const std::string &jsonString) {
        ConfigurationDraft draft{.baseRevision = Snapshot().Revision()};

        // Locate the values block if nested under "values": { ... }, otherwise use the entire JSON string.
        std::string targetString = jsonString;
        const std::size_t valuesPos = jsonString.find("\"values\"");
        if (valuesPos != std::string::npos) {
            const std::size_t openBrace = jsonString.find('{', valuesPos);
            if (openBrace != std::string::npos) {
                int depth = 1;
                std::size_t closeBrace = openBrace + 1;
                while (closeBrace < jsonString.size() && depth > 0) {
                    if (jsonString[closeBrace] == '{')
                        depth++;
                    else if (jsonString[closeBrace] == '}')
                        depth--;
                    closeBrace++;
                }
                if (depth == 0) {
                    targetString = jsonString.substr(openBrace, closeBrace - openBrace);
                }
            }
        }

        // Match key-value pairs: "key": value
        const std::regex kvRegex(
            R"regex("([^"\\]*(?:\\.[^"\\]*)*)"\s*:\s*(true|false|-?\d+|"([^"\\]*(?:\\.[^"\\]*)*)"))regex");
        auto begin = std::sregex_iterator(targetString.begin(), targetString.end(), kvRegex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            const std::smatch &match = *it;
            const std::string key = UnescapeJsonString(match[1].str());
            const std::string rawValue = match[2].str();

            const SettingKey settingKey{key};
            const auto descriptorIt = m_schema.m_descriptors.find(settingKey);
            if (descriptorIt == m_schema.m_descriptors.end()) {
                // Skip or fail on unknown keys: according to our engine validation rules, we skip unknown keys during load
                // or validate known keys.
                continue;
            }

            const SettingValueType type = descriptorIt->second.type;
            if (type == SettingValueType::Boolean) {
                if (rawValue == "true")
                    draft.proposedValues[settingKey] = true;
                else if (rawValue == "false")
                    draft.proposedValues[settingKey] = false;
                else
                    return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::JsonParseError));
            } else if (type == SettingValueType::Integer) {
                try {
                    draft.proposedValues[settingKey] = std::stoll(rawValue);
                } catch (...) {
                    return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::JsonParseError));
                }
            } else if (type == SettingValueType::String) {
                if (match[3].matched) {
                    draft.proposedValues[settingKey] = UnescapeJsonString(match[3].str());
                } else {
                    return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::JsonParseError));
                }
            }
        }

        return Commit(draft);
    }

    /** @copydoc ConfigurationService::LoadFile */
    Result<void> ConfigurationService::LoadFile(const std::string &path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::FileNotFound));
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return LoadJson(buffer.str());
    }

    /** @copydoc ConfigurationService::SaveFile */
    Result<void> ConfigurationService::SaveFile(const std::string &path) const {
        const std::string jsonContent = Snapshot().ToJson();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::FileWriteError));
        }
        output.write(jsonContent.data(), static_cast<std::streamsize>(jsonContent.size()));
        output.flush();
        if (!output) {
            return Result<void>::Failure(ConfigurationSchema::ErrorFor(ConfigurationErrors::FileWriteError));
        }
        return Result<void>::Success();
    }
} // namespace Horo
