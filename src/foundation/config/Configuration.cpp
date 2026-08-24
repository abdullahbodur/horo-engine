#include "Horo/Foundation/Configuration.h"

#include "../FoundationErrors.h"

#include <charconv>
#include <fstream>
#include <regex>
#include <sstream>
#include <string_view>

namespace Horo {
    namespace {
        [[nodiscard]] std::string_view SettingTypeName(const SettingValueType type) noexcept {
            using enum SettingValueType;
            switch (type) {
                case Boolean:
                    return "boolean";
                case Integer:
                    return "integer";
                case String:
                    return "string";
            }
            return "unknown";
        }

        [[nodiscard]] std::string_view SettingValueTypeName(const SettingValue &value) noexcept {
            if (std::holds_alternative<bool>(value))
                return "boolean";
            if (std::holds_alternative<std::int64_t>(value))
                return "integer";
            return "string";
        }

        [[nodiscard]] Error InvalidConfigurationValue(const SettingKey &key, const SettingValue &value,
                                                      const SettingDescriptor *descriptor) {
            if (descriptor == nullptr) {
                return MakeError(ConfigurationErrors::ValueInvalid,
                                 "Configuration key '" + key.Value() + "' is not registered in the active schema.");
            }
            return MakeError(ConfigurationErrors::ValueInvalid, "Configuration key '" + key.Value() + "' expects " +
                                                                    std::string{SettingTypeName(descriptor->type)} + " but received " +
                                                                    std::string{SettingValueTypeName(value)} + ".");
        }

        [[nodiscard]] std::string EscapeJsonString(const std::string_view value) {
            std::ostringstream escaped;
            for (const unsigned char character : value) {
                switch (character) {
                    case '"':
                        escaped << R"(\")";
                        break;
                    case '\\':
                        escaped << R"(\\)";
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
                            escaped << "\\u00" << hex[static_cast<std::size_t>(character) >> 4]
                                    << hex[static_cast<std::size_t>(character) & 0x0f];
                        } else {
                            escaped << static_cast<char>(character);
                        }
                }
            }
            return escaped.str();
        }

        void DecodeUnicodeEscape(const std::string_view input, std::size_t &idx, std::string &output) {
            if (idx + 5 < input.size()) {
                unsigned int codePoint = 0;
                const auto [ptr, ec] = std::from_chars(input.data() + idx + 2, input.data() + idx + 6, codePoint, 16);
                if (ec == std::errc{} && codePoint < 0x80) {
                    output.push_back(static_cast<char>(codePoint));
                } else {
                    output.push_back('?');
                }
                idx += 6;
            } else {
                output.push_back(input[idx]);
                ++idx;
            }
        }

        [[nodiscard]] std::string UnescapeJsonString(const std::string_view input) {
            std::string output;
            output.reserve(input.size());
            std::size_t idx = 0;
            while (idx < input.size()) {
                if (input[idx] == '\\' && idx + 1 < input.size()) {
                    switch (input[idx + 1]) {
                        case '"':
                            output.push_back('"');
                            idx += 2;
                            break;
                        case '\\':
                            output.push_back('\\');
                            idx += 2;
                            break;
                        case '/':
                            output.push_back('/');
                            idx += 2;
                            break;
                        case 'b':
                            output.push_back('\b');
                            idx += 2;
                            break;
                        case 'f':
                            output.push_back('\f');
                            idx += 2;
                            break;
                        case 'n':
                            output.push_back('\n');
                            idx += 2;
                            break;
                        case 'r':
                            output.push_back('\r');
                            idx += 2;
                            break;
                        case 't':
                            output.push_back('\t');
                            idx += 2;
                            break;
                        case 'u':
                            DecodeUnicodeEscape(input, idx, output);
                            break;
                        default:
                            output.push_back(input[idx + 1]);
                            idx += 2;
                            break;
                    }
                } else {
                    output.push_back(input[idx]);
                    idx++;
                }
            }
            return output;
        }

        [[nodiscard]] std::string ExtractValuesJson(const std::string &jsonString) {
            const std::size_t valuesPos = jsonString.find("\"values\"");
            if (valuesPos == std::string::npos)
                return jsonString;
            const std::size_t openBrace = jsonString.find('{', valuesPos);
            if (openBrace == std::string::npos)
                return jsonString;
            int depth = 1;
            std::size_t closeBrace = openBrace + 1;
            while (closeBrace < jsonString.size() && depth > 0) {
                if (jsonString[closeBrace] == '{')
                    depth++;
                else if (jsonString[closeBrace] == '}')
                    depth--;
                closeBrace++;
            }
            if (depth == 0)
                return jsonString.substr(openBrace, closeBrace - openBrace);
            return jsonString;
        }

        [[nodiscard]] Result<SettingValue> ParseSettingValue(const SettingValueType type, const std::string &rawValue,
                                                             const std::smatch &match) {
            using enum SettingValueType;
            if (type == Boolean) {
                if (rawValue == "true")
                    return Result<SettingValue>::Success(true);
                if (rawValue == "false")
                    return Result<SettingValue>::Success(false);
                return Result<SettingValue>::Failure(MakeError(ConfigurationErrors::JsonParseError));
            }
            if (type == Integer) {
                try {
                    return Result<SettingValue>::Success(std::stoll(rawValue));
                } catch (const std::invalid_argument &) {
                    return Result<SettingValue>::Failure(MakeError(ConfigurationErrors::JsonParseError));
                } catch (const std::out_of_range &) {
                    return Result<SettingValue>::Failure(MakeError(ConfigurationErrors::JsonParseError));
                }
            }
            if (type == String) {
                if (match[3].matched)
                    return Result<SettingValue>::Success(UnescapeJsonString(match[3].str()));
                return Result<SettingValue>::Failure(MakeError(ConfigurationErrors::JsonParseError));
            }
            return Result<SettingValue>::Failure(MakeError(ConfigurationErrors::JsonParseError));
        }

    }  // namespace

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
        json << "  \"schemaVersion\": 1,\n";
        json << "  \"revision\": " << m_data->revision << ",\n";
        json << "  \"values\": {\n";
        bool first = true;
        for (const auto &[key, value] : m_data->values) {
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
        using enum SettingValueType;
        return (type == Boolean && std::holds_alternative<bool>(value)) ||
               (type == Integer && std::holds_alternative<std::int64_t>(value)) ||
               (type == String && std::holds_alternative<std::string>(value));
    }

    /** @copydoc ConfigurationSchema::ErrorFor */
    Error ConfigurationSchema::ErrorFor(const ErrorCodeDescriptor &descriptor) {
        return MakeError(descriptor);
    }

    /** @copydoc ConfigurationSchema::Register */
    Result<void> ConfigurationSchema::Register(const SettingDescriptor &descriptor) {
        if (m_sealed || m_descriptors.contains(descriptor.key) || !MatchesType(descriptor.type, descriptor.defaultValue)) {
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
        for (const auto &[key, descriptor] : m_schema.m_descriptors) {
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
        for (const auto &[key, value] : draft.proposedValues) {
            const auto descriptor = m_schema.m_descriptors.find(key);
            if (descriptor == m_schema.m_descriptors.end()) {
                return Result<void>::Failure(InvalidConfigurationValue(key, value, nullptr));
            }
            if (!ConfigurationSchema::MatchesType(descriptor->second.type, value)) {
                return Result<void>::Failure(InvalidConfigurationValue(key, value, &descriptor->second));
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
            for (const auto &[key, value] : draft.proposedValues) {
                const auto descriptor = m_schema.m_descriptors.find(key);
                if (descriptor == m_schema.m_descriptors.end()) {
                    return Result<void>::Failure(InvalidConfigurationValue(key, value, nullptr));
                }
                if (!ConfigurationSchema::MatchesType(descriptor->second.type, value)) {
                    return Result<void>::Failure(InvalidConfigurationValue(key, value, &descriptor->second));
                }
                candidate->values[key] = value;
            }
            candidate->revision = m_active->revision + 1;
            m_active = std::move(candidate);
        }
        if (m_events != nullptr) {
            ConfigurationChangedEvent event{.revision = Snapshot().Revision()};
            event.changedKeys.reserve(draft.proposedValues.size());
            for (const auto &[key, _] : draft.proposedValues) {
                event.changedKeys.push_back(key);
            }
            m_events->Publish(event);
        }
        return Result<void>::Success();
    }

    /** @copydoc ConfigurationService::LoadJson */
    Result<void> ConfigurationService::LoadJson(const std::string &jsonString) {
        ConfigurationDraft draft{.baseRevision = Snapshot().Revision()};
        const std::string targetString = ExtractValuesJson(jsonString);

        // Match key-value pairs: "key": value
        const std::regex kvRegex(R"regex("([^"\\]*(?:\\.[^"\\]*)*)"\s*:\s*(true|false|-?\d+|"([^"\\]*(?:\\.[^"\\]*)*)"))regex");
        auto begin = std::sregex_iterator(targetString.begin(), targetString.end(), kvRegex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            const std::smatch &match = *it;
            const std::string key = UnescapeJsonString(match[1].str());
            const std::string rawValue = match[2].str();

            const SettingKey settingKey{key};
            const auto descriptorIt = m_schema.m_descriptors.find(settingKey);
            if (descriptorIt == m_schema.m_descriptors.end()) {
                continue;
            }

            auto parsed = ParseSettingValue(descriptorIt->second.type, rawValue, match);
            if (parsed.HasError())
                return Result<void>::Failure(parsed.ErrorValue());
            draft.proposedValues[settingKey] = std::move(parsed).Value();
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
}  // namespace Horo
