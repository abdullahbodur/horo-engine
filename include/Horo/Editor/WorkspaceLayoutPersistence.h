#pragma once

#include "Horo/Editor/WorkspaceLayout.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Horo::Editor
{
/** @brief Serializes and restores the versioned editor workspace layout document. */
class WorkspaceLayoutPersistence
{
  public:
    static constexpr std::uint32_t CurrentSchemaVersion = 1;

    [[nodiscard]] static std::string Serialize(const WorkspaceLayout &layout);
    [[nodiscard]] static std::optional<WorkspaceLayout> Deserialize(std::string_view json,
                                                                    std::string *error = nullptr);
    [[nodiscard]] static bool Save(const std::filesystem::path &path, const WorkspaceLayout &layout,
                                   std::string *error = nullptr);
    [[nodiscard]] static std::optional<WorkspaceLayout> Load(const std::filesystem::path &path,
                                                             std::string *error = nullptr);
};
} // namespace Horo::Editor
