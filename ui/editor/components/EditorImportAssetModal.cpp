/** @file EditorImportAssetModal.cpp
 *  @brief Two-panel "Import Asset" modal implementation. See EditorImportAssetModal.h.
 *
 *  Left panel: drag-and-drop zone + file browser with thumbnail grid.
 *  Right panel: selected file info + import settings.
 */
#include "ui/editor/components/EditorImportAssetModal.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>

#include "core/StringUtils.h"
#include "renderer/FbxLoader.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/EditorFilePickerUtils.h"

namespace Horo::Editor {
namespace {

/** @brief Loads recent imports from ~/.horo/settings.json (file-system helper). */
std::vector<std::string> LoadRecentImportsJson() {
    std::vector<std::string> result;
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t len = 0;
    _dupenv_s(&userProfile, &len, "USERPROFILE");
    const std::filesystem::path homeDir = userProfile ? userProfile : ".";
    free(userProfile);
#else
    const std::filesystem::path homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
#endif
    const std::filesystem::path settingsPath = homeDir / ".horo" / "settings.json";
    std::error_code ec;
    if (!std::filesystem::exists(settingsPath, ec))
        return result;

    try {
        std::ifstream file(settingsPath);
        if (!file.is_open())
            return result;
        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        // Simple JSON parsing for recent_imports array
        auto start = content.find("\"recent_imports\"");
        if (start == std::string::npos)
            return result;
        start = content.find('[', start);
        if (start == std::string::npos)
            return result;
        auto end = content.find(']', start);
        if (end == std::string::npos)
            return result;
        std::string arrayStr = content.substr(start + 1, end - start - 1);
        size_t pos = 0;
        while ((pos = arrayStr.find('"', pos)) != std::string::npos) {
            auto endQuote = arrayStr.find('"', pos + 1);
            if (endQuote == std::string::npos) break;
            result.push_back(arrayStr.substr(pos + 1, endQuote - pos - 1));
            pos = endQuote + 1;
            if (result.size() >= 6) break;
        }
    } catch (...) {
        // Ignore parse errors
    }
    return result;
}

/** @brief Saves a recent import to ~/.horo/settings.json. */
void SaveRecentImport(std::string_view path) {
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t len = 0;
    _dupenv_s(&userProfile, &len, "USERPROFILE");
    const std::filesystem::path homeDir = userProfile ? userProfile : ".";
    free(userProfile);
#else
    const std::filesystem::path homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
#endif
    const std::filesystem::path settingsDir = homeDir / ".horo";
    const std::filesystem::path settingsPath = settingsDir / "settings.json";

    std::error_code ec;
    std::filesystem::create_directories(settingsDir, ec);

    // Load existing
    auto recent = LoadRecentImportsJson();

    // Remove if already present, add to front
    recent.erase(std::remove(recent.begin(), recent.end(), std::string(path)), recent.end());
    recent.insert(recent.begin(), std::string(path));
    if (recent.size() > 6)
        recent.resize(6);

    // Write
    try {
        std::ofstream file(settingsPath);
        if (!file.is_open()) return;
        file << "{\n  \"recent_imports\": [\n";
        for (size_t i = 0; i < recent.size(); ++i) {
            file << "    \"" << recent[i] << "\"";
            if (i + 1 < recent.size()) file << ",";
            file << "\n";
        }
        file << "  ]\n}\n";
    } catch (...) {
        // Ignore write errors
    }
}

} // namespace

/** @copydoc GetTextureSlotLabel */
const char *GetTextureSlotLabel(TextureSlotKind slot) {
    switch (slot) {
    case TextureSlotKind::Albedo: return "Albedo";
    case TextureSlotKind::Normal: return "Normal";
    case TextureSlotKind::MetallicRoughness: return "Metallic+Roughness";
    case TextureSlotKind::Emissive: return "Emissive";
    case TextureSlotKind::Occlusion: return "Occlusion";
    default: return "Unknown";
    }
}

/** @copydoc EditorImportAssetModal::Open */
void EditorImportAssetModal::Open(std::string_view initialSourcePath,
                                   const AssetImporterRegistry *registry,
                                   const std::filesystem::path& projectRoot) {
    m_registry = registry;
    m_open = true;
    m_hasPendingRequest = false;
    m_hasResult = false;
    m_assetIdAutoDerived = true;
    m_displayNameAutoDerived = true;
    m_renderScaleAutoDerived = true;
    m_tab = Tab::FromFile;

    // Reset draft
    m_draft.sourcePath.clear();
    m_draft.assetId.clear();
    m_draft.displayName.clear();
    m_draft.importerId.clear();
    m_draft.meshPath.clear();
    m_draft.renderScale = "1.0000,1.0000,1.0000";
    for (auto& slot : m_draft.textureSlots) {
        slot.path.clear();
        slot.autoDetectedPath.clear();
        slot.hasAutoDetected = false;
        slot.userOverride = false;
        slot.enabled = false;
    }
    m_draft.settings = ImportSettings{};
    m_draft.settings.contentLocation = projectRoot.generic_string();

    // Seed from initial path
    if (!initialSourcePath.empty()) {
        m_draft.sourcePath = std::string(initialSourcePath);
        RefreshImporterFromExtension();
        RefreshIdentitiesFromPath();
        RefreshTextureSlotsFromFbxPreview();
    }

    // Setup file browser
    m_fileBrowser.SetRootPath(projectRoot);
    if (!initialSourcePath.empty()) {
        std::filesystem::path srcPath(initialSourcePath);
        m_fileBrowser.NavigateTo(srcPath.parent_path());
        // Select the file
        for (auto& entry : m_fileBrowser.State().entries) {
            if (entry.fullPath == srcPath.generic_string()) {
                // Select it
                break;
            }
        }
    } else {
        m_fileBrowser.NavigateTo(projectRoot);
    }

    // Load recent imports
    m_recentImports = LoadRecentImportsJson();
}

/** @copydoc EditorImportAssetModal::Close */
void EditorImportAssetModal::Close() {
    m_open = false;
    m_hasPendingRequest = false;
    m_hasResult = false;
    m_draft.sourcePath.clear();
    m_draft.assetId.clear();
    m_draft.displayName.clear();
    m_draft.importerId.clear();
    m_draft.meshPath.clear();
    m_draft.renderScale.clear();
    for (auto& slot : m_draft.textureSlots) {
        slot.path.clear();
        slot.autoDetectedPath.clear();
        slot.hasAutoDetected = false;
        slot.userOverride = false;
        slot.enabled = false;
    }
    m_draft.settings = ImportSettings{};
    m_lastResult = {};
    m_recentImports.clear();
}

/** @copydoc EditorImportAssetModal::Draw */
void EditorImportAssetModal::Draw() {
    if (!m_open)
        return;
    if (ImGui::GetCurrentContext() == nullptr)
        return; // Headless / unit-test context

    constexpr const char *kPopupId = "Import Asset##HoroEditor";
    if (!ImGui::IsPopupOpen(kPopupId))
        ImGui::OpenPopup(kPopupId);

    const ImVec2 modalSize(1200, 800);
    const ImVec2 viewportCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(ImVec2(viewportCenter.x - modalSize.x * 0.5f,
                                    viewportCenter.y - modalSize.y * 0.5f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);

    if (!ImGui::BeginPopupModal(kPopupId, &m_open, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
        return;
    }

    // Custom title bar with themed close button
    const auto &theme = Ui::GetEditorTheme();
    if (Ui::RenderModalTitleBar(theme, "Import Asset")) {
        Close();
    }

    // Capture modal rect for drag-and-drop hit testing
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 windowMax = ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
    SetModalRect(windowMin.x, windowMin.y, windowMax.x, windowMax.y);

    ImGui::TextUnformatted("Import 3D models, materials and textures into your project.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Two-panel layout — content flows naturally, modal sized to fit
    const float leftPanelW = 560.0f;
    if (ImGui::BeginTable("##ImportPanels", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthFixed, leftPanelW);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        DrawLeftPanel();
        ImGui::TableNextColumn();
        DrawRightPanel();
        ImGui::EndTable();
    }

    if (m_hasResult)
        DrawResultPanel();

    DrawActionsSection();

    ImGui::EndPopup();

    if (!m_open)
        ImGui::CloseCurrentPopup();
}

/** @copydoc EditorImportAssetModal::DrawLeftPanel */
void EditorImportAssetModal::DrawLeftPanel() {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float dropZoneH = 80.0f;
    const float gridH = 480.0f; // Fits ~6 items before scrolling

    // Compact drag & drop zone (~80px)
    {
        ImGui::InvisibleButton("##DropZone", ImVec2(availW, dropZoneH));
        const bool isHovered = ImGui::IsItemHovered();

        const ImVec2 tileMin = ImGui::GetItemRectMin();
        const ImVec2 tileMax = ImGui::GetItemRectMax();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec4 bgColor = isHovered
                                   ? ImVec4(0.14f, 0.20f, 0.30f, 0.95f)
                                   : ImVec4(0.09f, 0.12f, 0.18f, 0.85f);
        dl->AddRectFilled(tileMin, tileMax,
                          ImGui::ColorConvertFloat4ToU32(bgColor), 6.0f);

        const ImVec4 borderColor = isHovered
                                       ? ImVec4(0.45f, 0.62f, 0.90f, 0.90f)
                                       : ImVec4(0.35f, 0.50f, 0.70f, 0.50f);
        dl->AddRect(tileMin, tileMax,
                    ImGui::ColorConvertFloat4ToU32(borderColor), 6.0f, ImDrawFlags_RoundCornersAll, 1.5f);

        const std::string labelText = "Drag and drop a file here";
        const ImVec2 labelSz = ImGui::CalcTextSize(labelText.c_str());
        const ImVec2 labelPos = ImVec2(
            tileMin.x + (availW - labelSz.x) * 0.5f,
            tileMin.y + (dropZoneH - labelSz.y) * 0.5f);
        const ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f));
        dl->AddText(labelPos, labelCol, labelText.c_str());
    }

    ImGui::Spacing();

    // File browser: nav/search fixed, grid scrolls internally
    if (m_fileBrowser.Draw(gridH)) {
        if (m_fileBrowser.HasSelection()) {
            m_draft.sourcePath = m_fileBrowser.GetSelectedFilePath();
            RefreshImporterFromExtension();
            RefreshIdentitiesFromPath();
            RefreshTextureSlotsFromFbxPreview();
        }
    }

    // Status bar at the bottom
    m_fileBrowser.DrawStatusBar();
}

/** @copydoc EditorImportAssetModal::DrawRightPanel */
void EditorImportAssetModal::DrawRightPanel() {
    DrawSelectedFileInfo();
    ImGui::Separator();
    ImGui::Spacing();
    DrawImportSettings();
}

/** @copydoc EditorImportAssetModal::DrawSelectedFileInfo */
void EditorImportAssetModal::DrawSelectedFileInfo() {
    ImGui::TextUnformatted("Selected File");
    ImGui::Spacing();

    const std::string& selectedPath = m_fileBrowser.GetSelectedFilePath();
    if (selectedPath.empty()) {
        ImGui::TextDisabled("No file selected");
        return;
    }

    std::filesystem::path filePath(selectedPath);
    std::string fileName = filePath.filename().string();
    std::string fileDir = filePath.parent_path().generic_string();
    uint64_t fileSize = 0;
    std::error_code ec;
    if (std::filesystem::is_regular_file(filePath, ec))
        fileSize = std::filesystem::file_size(filePath, ec);

    // File icon + name
    ImGui::Text("%s  %s", ICON_FA_CUBE, fileName.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", Horo::FormatFileSize(fileSize).c_str());

    // Full path
    ImGui::TextDisabled("%s", fileDir.c_str());
}

/** @copydoc EditorImportAssetModal::DrawImportSettings */
void EditorImportAssetModal::DrawImportSettings() {
    ImGui::TextUnformatted("Import Settings");
    ImGui::Spacing();

    constexpr float kLabelColW = 200.0f;

    // Helper: one form row — label on left, field on right
    auto FormRow = [&](const char* label, auto&& drawField) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        drawField();
    };

    if (ImGui::BeginTable("##ImportSettingsForm", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kLabelColW);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);

        // Import As
        FormRow("Import As", [&]() {
            const char* importTypes[] = {"Static Mesh", "Skeletal Mesh", "Texture"};
            const bool isSkeletal = m_draft.settings.importType == "skeletal_mesh";
            const bool isTexture = m_draft.settings.importType == "texture";
            int currentIdx = isSkeletal ? 1 : (isTexture ? 2 : 0);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##ImportType", importTypes[currentIdx])) {
                for (int i = 0; i < 3; i++) {
                    const bool isSelected = (i == currentIdx);
                    if (i > 0) {
                        ImGui::BeginDisabled();
                        ImGui::Selectable(importTypes[i], false);
                        ImGui::EndDisabled();
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    } else {
                        if (ImGui::Selectable(importTypes[i], isSelected)) {
                            m_draft.settings.importType = "static_mesh";
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        });

        // Content Location
        FormRow("Content Location", [&]() {
            char locBuf[512]{};
            m_draft.settings.contentLocation.copy(locBuf, sizeof(locBuf) - 1);
            ImGui::SetNextItemWidth(-140.0f);
            if (ImGui::InputText("##ContentLocation", locBuf, sizeof(locBuf))) {
                m_draft.settings.contentLocation = locBuf;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Browse##ContentLocation")) {
                // Could open folder picker here
            }
        });

        // Asset Name
        FormRow("Asset Name", [&]() {
            char nameBuf[256]{};
            m_draft.assetId.copy(nameBuf, sizeof(nameBuf) - 1);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##AssetName", nameBuf, sizeof(nameBuf))) {
                m_draft.assetId = nameBuf;
                m_assetIdAutoDerived = false;
            }
        });

        // Scale
        FormRow("Scale", [&]() {
            float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
#ifdef _WIN32
            sscanf_s(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#else
            sscanf(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#endif
            ImGui::SetNextItemWidth(70);
            char scaleXBuf[32];
            snprintf(scaleXBuf, sizeof(scaleXBuf), "%.4f", scaleX);
            if (ImGui::InputText("##ScaleX", scaleXBuf, sizeof(scaleXBuf))) {
                scaleX = std::strtof(scaleXBuf, nullptr);
                m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            char scaleYBuf[32];
            snprintf(scaleYBuf, sizeof(scaleYBuf), "%.4f", scaleY);
            if (ImGui::InputText("##ScaleY", scaleYBuf, sizeof(scaleYBuf))) {
                scaleY = std::strtof(scaleYBuf, nullptr);
                m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            char scaleZBuf[32];
            snprintf(scaleZBuf, sizeof(scaleZBuf), "%.4f", scaleZ);
            if (ImGui::InputText("##ScaleZ", scaleZBuf, sizeof(scaleZBuf))) {
                scaleZ = std::strtof(scaleZBuf, nullptr);
                m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
            }
            ImGui::SameLine();
            if (m_renderScaleAutoDerived) {
                ImGui::TextDisabled("(auto)");
            } else {
                if (ImGui::SmallButton("Reset##ResetScale")) {
                    m_draft.renderScale = "1.0000,1.0000,1.0000";
                    m_renderScaleAutoDerived = true;
                }
            }
        });

        // Checkboxes — each on its own row
        const auto& theme = Ui::GetEditorTheme();
        FormRow("Auto Generate Collision", [&]() {
            Ui::RenderEditorCheckbox(theme, "##AutoCollision",
                                     m_draft.settings.autoGenerateCollision);
        });
        FormRow("Import Materials", [&]() {
            Ui::RenderEditorCheckbox(theme, "##ImportMaterials",
                                     m_draft.settings.importMaterials);
        });
        FormRow("Import Textures", [&]() {
            Ui::RenderEditorCheckbox(theme, "##ImportTextures",
                                     m_draft.settings.importTextures);
        });
        FormRow("Combine Meshes", [&]() {
            ImGui::BeginDisabled();
            Ui::RenderEditorCheckbox(theme, "##CombineMeshes",
                                     m_draft.settings.combineMeshes);
            ImGui::EndDisabled();
        });
        FormRow("Transform Vertex to Absolute", [&]() {
            Ui::RenderEditorCheckbox(theme, "##TransformVertex",
                                     m_draft.settings.transformVertexToAbsolute);
        });

        // Normal Import Method
        FormRow("Normal Import Method", [&]() {
            const char* normalMethods[] = {"Import Normals", "Compute Normals", "None"};
            const char* normalKeys[] = {"import_normals", "compute_normals", "none"};
            int normalIdx = 0;
            for (int i = 0; i < 3; i++) {
                if (m_draft.settings.normalImportMethod == normalKeys[i]) {
                    normalIdx = i;
                    break;
                }
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##NormalMethod", normalMethods[normalIdx])) {
                for (int i = 0; i < 3; i++) {
                    const bool isSelected = (i == normalIdx);
                    if (ImGui::Selectable(normalMethods[i], isSelected)) {
                        m_draft.settings.normalImportMethod = normalKeys[i];
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });

        // Material Import Method
        FormRow("Material Import Method", [&]() {
            const char* matMethods[] = {"Create New Materials", "Reuse Existing", "None"};
            const char* matKeys[] = {"create_new", "reuse_existing", "none"};
            int matIdx = 0;
            for (int i = 0; i < 3; i++) {
                if (m_draft.settings.materialImportMethod == matKeys[i]) {
                    matIdx = i;
                    break;
                }
            }
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::BeginCombo("##MatMethod", matMethods[matIdx])) {
                for (int i = 0; i < 3; i++) {
                    const bool isSelected = (i == matIdx);
                    if (ImGui::Selectable(matMethods[i], isSelected)) {
                        m_draft.settings.materialImportMethod = matKeys[i];
                    }
                    if (isSelected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        });

        // Advanced Options (collapsible)
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableSetColumnIndex(1);
        if (ImGui::CollapsingHeader("Advanced Options")) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Remove Degenerates");
            ImGui::TableNextColumn();
            Ui::RenderEditorCheckbox(theme, "##RemoveDegenerates",
                                     m_draft.settings.removeDegenerates);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Optimize Mesh");
            ImGui::TableNextColumn();
            Ui::RenderEditorCheckbox(theme, "##OptimizeMesh",
                                     m_draft.settings.optimizeMesh);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Import Animations");
            ImGui::TableNextColumn();
            ImGui::BeginDisabled();
            Ui::RenderEditorCheckbox(theme, "##ImportAnimations",
                                     m_draft.settings.importAnimations);
            ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }
}

/** @copydoc EditorImportAssetModal::DrawFromFileTab */
void EditorImportAssetModal::DrawFromFileTab() {
    // The From File tab is now handled by DrawLeftPanel + DrawRightPanel
    // This method is kept for backward compatibility with the tab system
    DrawLeftPanel();
    ImGui::SameLine();
    DrawRightPanel();
}

/** @copydoc EditorImportAssetModal::DrawManualTab */
void EditorImportAssetModal::DrawManualTab() {
    ImGui::Spacing();
    DrawMeshPathSection();
    DrawIdentitySection();
    DrawRenderScaleSection();

    ImGui::Separator();

    // Texture slots (same as From File tab)
    DrawTextureSlotsSection();
}

/** @copydoc EditorImportAssetModal::DrawMeshPathSection */
void EditorImportAssetModal::DrawMeshPathSection() {
    ImGui::TextUnformatted("Mesh path");
    char meshBuffer[1024]{};
    const std::size_t copyLen =
        std::min<std::size_t>(m_draft.meshPath.size(), sizeof(meshBuffer) - 1);
    std::memcpy(meshBuffer, m_draft.meshPath.data(), copyLen);
    ImGui::SetNextItemWidth(250);
    if (ImGui::InputText("##MeshPath", meshBuffer, sizeof(meshBuffer))) {
        m_draft.meshPath.assign(meshBuffer);
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...##MeshBrowse")) {
        const std::string picked = PickMeshFilePath();
        if (!picked.empty()) {
            m_draft.meshPath = picked;
        }
    }
}

/** @copydoc EditorImportAssetModal::DrawIdentitySection */
void EditorImportAssetModal::DrawIdentitySection() {
    ImGui::Spacing();
    char idBuffer[256]{};
    const std::size_t idLen =
        std::min<std::size_t>(m_draft.assetId.size(), sizeof(idBuffer) - 1);
    std::memcpy(idBuffer, m_draft.assetId.data(), idLen);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Asset ID##ImportId", idBuffer, sizeof(idBuffer))) {
        m_draft.assetId.assign(idBuffer);
        m_assetIdAutoDerived = false;
    }

    char displayBuffer[256]{};
    const std::size_t displayLen = std::min<std::size_t>(
        m_draft.displayName.size(), sizeof(displayBuffer) - 1);
    std::memcpy(displayBuffer, m_draft.displayName.data(), displayLen);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Display Name##ImportName", displayBuffer,
                          sizeof(displayBuffer))) {
        m_draft.displayName.assign(displayBuffer);
        m_displayNameAutoDerived = false;
    }
}

/** @copydoc EditorImportAssetModal::DrawRenderScaleSection */
void EditorImportAssetModal::DrawRenderScaleSection() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Render scale");
    ImGui::SameLine();
    ImGui::TextDisabled("(X, Y, Z)");

    float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
    if (m_renderScaleAutoDerived && !m_draft.sourcePath.empty()) {
        m_draft.renderScale = "1.0000,1.0000,1.0000";
    }

#ifdef _WIN32
    sscanf_s(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#else
    sscanf(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#endif

    ImGui::SetNextItemWidth(80);
    char scaleXBuf[32];
    snprintf(scaleXBuf, sizeof(scaleXBuf), "%.4f", scaleX);
    if (ImGui::InputText("##ScaleX", scaleXBuf, sizeof(scaleXBuf))) {
        scaleX = std::strtof(scaleXBuf, nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char scaleYBuf[32];
    snprintf(scaleYBuf, sizeof(scaleYBuf), "%.4f", scaleY);
    if (ImGui::InputText("##ScaleY", scaleYBuf, sizeof(scaleYBuf))) {
        scaleY = std::strtof(scaleYBuf, nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    char scaleZBuf[32];
    snprintf(scaleZBuf, sizeof(scaleZBuf), "%.4f", scaleZ);
    if (ImGui::InputText("##ScaleZ", scaleZBuf, sizeof(scaleZBuf))) {
        scaleZ = std::strtof(scaleZBuf, nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    if (m_renderScaleAutoDerived) {
        ImGui::TextDisabled("(auto)");
    } else {
        if (ImGui::SmallButton("Reset##ResetScale")) {
            m_draft.renderScale = "1.0000,1.0000,1.0000";
            m_renderScaleAutoDerived = true;
        }
    }
}

/** @copydoc EditorImportAssetModal::DrawTextureSlotsSection */
void EditorImportAssetModal::DrawTextureSlotsSection() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Texture Slots");
    ImGui::Spacing();

    for (auto &slot: m_draft.textureSlots) {
        DrawTextureSlotRow(slot);
    }
}

/** @copydoc EditorImportAssetModal::DrawTextureSlotRow */
void EditorImportAssetModal::DrawTextureSlotRow(TextureSlotDraft &slot) {
    ImGui::PushID(static_cast<int>(slot.slot));

    const char *label = GetTextureSlotLabel(slot.slot);
    ImGui::TextUnformatted(label);

    ImGui::SetNextItemWidth(200);
    char pathBuffer[1024]{};
    const std::string &displayPath = slot.userOverride || !slot.path.empty()
                                        ? slot.path
                                        : (slot.hasAutoDetected ? slot.autoDetectedPath : "");
    const std::size_t copyLen = std::min<std::size_t>(displayPath.size(), sizeof(pathBuffer) - 1);
    std::memcpy(pathBuffer, displayPath.data(), copyLen);

    const bool isPlaceholder = slot.hasAutoDetected && !slot.userOverride && slot.path.empty();
    if (isPlaceholder) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    if (ImGui::InputText("##TexturePath", pathBuffer, sizeof(pathBuffer))) {
        slot.path.assign(pathBuffer);
        slot.userOverride = !pathBuffer[0];
        slot.enabled = !slot.path.empty();
    }

    if (isPlaceholder) {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Browse##BrowseTexture")) {
        const std::string picked = PickTextureFilePath();
        if (!picked.empty()) {
            slot.path = picked;
            slot.userOverride = true;
            slot.enabled = true;
        }
    }
    ImGui::SameLine();
    if (slot.enabled && ImGui::SmallButton("Clear##ClearTexture")) {
        slot.path.clear();
        slot.userOverride = false;
        slot.enabled = false;
    }

    if (slot.hasAutoDetected && !slot.userOverride) {
        ImGui::SameLine();
        ImGui::TextDisabled("(auto-detected)");
    }

    ImGui::PopID();
}

/** @copydoc EditorImportAssetModal::DrawActionsSection */
void EditorImportAssetModal::DrawActionsSection() {
    ImGui::Spacing();
    ImGui::Separator();

    const bool canImport = m_fileBrowser.HasSelection() &&
                           !m_draft.assetId.empty() &&
                           !m_draft.importerId.empty();

    if (m_hasResult) {
        // Post-import: show "Discard result and close" or just "Close"
        if (ImGui::Button("Close")) {
            Close();
        }
        if (!m_lastResult.ok || !m_lastResult.diagnostics.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Discard result and close")) {
                m_hasResult = false;
                m_lastResult = {};
            }
        }
    } else {
        // Pre-import: Cancel / Import
        if (!canImport)
            ImGui::BeginDisabled();
        if (ImGui::Button("Import")) {
            m_hasPendingRequest = true;
            // Save to recent imports
            if (!m_draft.sourcePath.empty())
                SaveRecentImport(m_draft.sourcePath);
        }
        if (!canImport)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            Close();
        }
    }
}

/** @copydoc EditorImportAssetModal::DrawResultPanel */
void EditorImportAssetModal::DrawResultPanel() const {
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (m_lastResult.ok) {
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Import successful");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Import failed: %s",
                           m_lastResult.error.c_str());
    }

    for (const auto &diag: m_lastResult.diagnostics) {
        ImVec4 color;
        switch (diag.severity) {
        case AssetDiagnosticSeverity::Info:
            color = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);
            break;
        case AssetDiagnosticSeverity::Warning:
            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            break;
        case AssetDiagnosticSeverity::Error:
            color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            break;
        default:
            color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
        ImGui::TextColored(color, "[%s] %s",
                           diag.severity == AssetDiagnosticSeverity::Info ? "INFO" :
                           diag.severity == AssetDiagnosticSeverity::Warning ? "WARN" : "ERR",
                           diag.message.c_str());
    }
}

/** @copydoc EditorImportAssetModal::RefreshImporterFromExtension */
void EditorImportAssetModal::RefreshImporterFromExtension() {
    if (m_registry == nullptr || m_draft.sourcePath.empty())
        return;
    if (!m_draft.importerId.empty() && !m_assetIdAutoDerived)
        return; // User manually selected an importer

    const AssetImporter *importer = m_registry->FindByExtension(m_draft.sourcePath);
    if (importer)
        m_draft.importerId = importer->ImporterId();
    else
        m_draft.importerId.clear();
}

/** @copydoc EditorImportAssetModal::RefreshIdentitiesFromPath */
void EditorImportAssetModal::RefreshIdentitiesFromPath() {
    if (m_draft.sourcePath.empty())
        return;

    if (m_assetIdAutoDerived)
        m_draft.assetId = AssetIdFromImportedPath(m_draft.sourcePath);
    if (m_displayNameAutoDerived)
        m_draft.displayName = m_draft.assetId;
}

/** @copydoc EditorImportAssetModal::RefreshTextureSlotsFromFbxPreview */
void EditorImportAssetModal::RefreshTextureSlotsFromFbxPreview() {
    if (m_draft.sourcePath.empty())
        return;

    std::filesystem::path srcPath(m_draft.sourcePath);
    std::string ext = Horo::ToLowerAscii(srcPath.extension().string());
    if (ext != ".fbx")
        return;

    // Load FBX to extract texture records
    try {
        FbxLoader::FbxLoadResult result = FbxLoader::LoadStaticMesh(m_draft.sourcePath);
        if (!result.ok || result.textures.empty())
            return;

        for (const auto &tex: result.textures) {
            TextureSlotKind slotKind = TextureSlotKind::Albedo;
            switch (tex.slot) {
            case FbxLoader::FbxTextureSlot::Albedo: slotKind = TextureSlotKind::Albedo; break;
            case FbxLoader::FbxTextureSlot::Normal: slotKind = TextureSlotKind::Normal; break;
            case FbxLoader::FbxTextureSlot::MetallicRoughness: slotKind = TextureSlotKind::MetallicRoughness; break;
            case FbxLoader::FbxTextureSlot::Emissive: slotKind = TextureSlotKind::Emissive; break;
            case FbxLoader::FbxTextureSlot::Occlusion: slotKind = TextureSlotKind::Occlusion; break;
            default: continue;
            }

            auto &slot = m_draft.textureSlots[static_cast<int>(slotKind)];
            // Prefer absolute path, fall back to relative, then filename
            slot.autoDetectedPath = !tex.absolutePath.empty() ? tex.absolutePath
                                    : !tex.relativePath.empty() ? tex.relativePath
                                    : tex.filename;
            slot.hasAutoDetected = true;
            slot.enabled = true;
        }
    } catch (...) {
        // Ignore FBX load errors during preview
    }
}

/** @copydoc EditorImportAssetModal::ConsumePendingRequest */
ImportAssetRequest EditorImportAssetModal::ConsumePendingRequest() {
    ImportAssetRequest req;
    req.sourcePath = m_fileBrowser.HasSelection()
                         ? m_fileBrowser.GetSelectedFilePath()
                         : m_draft.sourcePath;
    req.assetId = m_draft.assetId;
    req.displayName = m_draft.displayName.empty() ? req.assetId : m_draft.displayName;
    req.importerId = m_draft.importerId;
    req.settings = BuildSettingsMap();

    // Build texture overrides
    TextureOverrides overrides;
    bool hasAnyOverride = false;
    for (const auto &slot: m_draft.textureSlots) {
        if (!slot.path.empty()) {
            hasAnyOverride = true;
            switch (slot.slot) {
            case TextureSlotKind::Albedo: overrides.albedoMap = slot.path; break;
            case TextureSlotKind::Normal: overrides.normalMap = slot.path; break;
            case TextureSlotKind::MetallicRoughness: overrides.metallicRoughnessMap = slot.path; break;
            case TextureSlotKind::Emissive: overrides.emissiveMap = slot.path; break;
            case TextureSlotKind::Occlusion: overrides.occlusionMap = slot.path; break;
            }
        }
    }
    if (hasAnyOverride)
        req.textureOverrides = overrides;

    m_hasPendingRequest = false;
    return req;
}

/** @copydoc EditorImportAssetModal::SetLastResult */
void EditorImportAssetModal::SetLastResult(const ImportAssetOutcome &outcome) {
    m_lastResult = outcome;
    m_hasResult = true;
    if (outcome.ok) {
        const bool hasErrors = std::ranges::any_of(outcome.diagnostics, [](const auto& d) {
            return d.severity == AssetDiagnosticSeverity::Error;
        });
        if (!hasErrors)
            m_open = false;
    }
}

/** @copydoc EditorImportAssetModal::SetDraftForTest */
void EditorImportAssetModal::SetDraftForTest(std::string_view sourcePath,
                                              std::string_view assetId,
                                              std::string_view displayName,
                                              std::string_view importerId) {
    m_draft.sourcePath = std::string(sourcePath);
    m_draft.assetId = std::string(assetId);
    m_draft.displayName = std::string(displayName);
    m_draft.importerId = std::string(importerId);
    m_assetIdAutoDerived = false;
    m_displayNameAutoDerived = false;
}

/** @copydoc EditorImportAssetModal::RequestImportForTest */
void EditorImportAssetModal::RequestImportForTest() {
    m_hasPendingRequest = true;
}

/** @copydoc EditorImportAssetModal::HandleFileDrop */
bool EditorImportAssetModal::HandleFileDrop(float x, float y, const std::string &path) {
    // Delegate to file browser
    return m_fileBrowser.HandleFileDrop(x, y, path);
}

/** @copydoc EditorImportAssetModal::LoadRecentImports */
void EditorImportAssetModal::LoadRecentImports() {
    m_recentImports = LoadRecentImportsJson();
}

/** @copydoc EditorImportAssetModal::SaveRecentImport */
void EditorImportAssetModal::SaveRecentImport(std::string_view path) {
    ::Horo::Editor::SaveRecentImport(path);
}

/** @copydoc EditorImportAssetModal::ClearRecentImports */
void EditorImportAssetModal::ClearRecentImports() {
    m_recentImports.clear();
#ifdef _WIN32
    char* userProfile = nullptr;
    size_t len = 0;
    _dupenv_s(&userProfile, &len, "USERPROFILE");
    const std::filesystem::path homeDir = userProfile ? userProfile : ".";
    free(userProfile);
#else
    const std::filesystem::path homeDir = std::getenv("HOME") ? std::getenv("HOME") : ".";
#endif
    const std::filesystem::path settingsPath = homeDir / ".horo" / "settings.json";
    std::error_code ec;
    if (std::filesystem::exists(settingsPath, ec)) {
        // Rewrite with empty array
        try {
            std::ofstream file(settingsPath);
            if (file.is_open())
                file << "{\n  \"recent_imports\": []\n}\n";
        } catch (...) {}
    }
}

/** @copydoc EditorImportAssetModal::BuildSettingsMap */
std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>
EditorImportAssetModal::BuildSettingsMap() const {
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> settings;
    settings["import.collision"] = m_draft.settings.autoGenerateCollision ? "true" : "false";
    settings["import.materials"] = m_draft.settings.importMaterials ? "true" : "false";
    settings["import.textures"] = m_draft.settings.importTextures ? "true" : "false";
    settings["import.combine_meshes"] = m_draft.settings.combineMeshes ? "true" : "false";
    settings["import.normals"] = m_draft.settings.normalImportMethod;
    settings["import.material_method"] = m_draft.settings.materialImportMethod;
    settings["import.remove_degenerates"] = m_draft.settings.removeDegenerates ? "true" : "false";
    settings["import.optimize_mesh"] = m_draft.settings.optimizeMesh ? "true" : "false";
    settings["import.animations"] = m_draft.settings.importAnimations ? "true" : "false";
    settings["import.content_location"] = m_draft.settings.contentLocation;
    return settings;
}

} // namespace Horo::Editor
