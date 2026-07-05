/** @file EditorImportAssetModal.cpp
 *  @brief Two-panel "Import Asset" modal implementation. See EditorImportAssetModal.h.
 *
 *  Left panel: drag-and-drop zone + file browser with thumbnail grid.
 *  Right panel: selected file info + import settings.
 */
#include "ui/editor/components/EditorImportAssetModal.h"

#include <algorithm>
#include <array>
#include <cstdio>
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
#include "core/Logger.h"
#include "renderer/FbxLoader.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"
#include "ui/editor/AssetImportService.h"
#include "ui/editor/EditorAssetImport.h"
#include "ui/editor/EditorFilePickerUtils.h"

namespace Horo::Editor {
namespace {

/** @brief Copies text into a fixed ImGui input buffer with null termination. */
template <std::size_t N>
std::array<char, N> MakeInputBuffer(std::string_view text) {
    std::array<char, N> buffer{};
    const std::size_t copyLen = std::min<std::size_t>(text.size(), buffer.size() - 1);
    std::memcpy(buffer.data(), text.data(), copyLen);
    return buffer;
}

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
    if (std::error_code ec; !std::filesystem::exists(settingsPath, ec))
        return result;

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
    pos = arrayStr.find('"', pos);
    while (result.size() < 6 && pos != std::string::npos) {
        auto endQuote = arrayStr.find('"', pos + 1);
        if (endQuote == std::string::npos) {
            break;
        }
        result.push_back(arrayStr.substr(pos + 1, endQuote - pos - 1));
        pos = arrayStr.find('"', endQuote + 1);
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
    std::erase(recent, std::string(path));
    recent.emplace(recent.begin(), path);
    if (recent.size() > 6)
        recent.resize(6);

    // Write
    std::ofstream file(settingsPath);
    if (!file.is_open()) {
        LogWarn("[ImportAssetModal] Failed to open recent imports settings for writing: {}",
                settingsPath.string());
        return;
    }
    file << "{\n  \"recent_imports\": [\n";
    for (size_t i = 0; i < recent.size(); ++i) {
        file << "    \"" << recent[i] << "\"";
        if (i + 1 < recent.size()) file << ",";
        file << "\n";
    }
    file << "  ]\n}\n";
}

/** @brief Draws one import-type combo option and reports a user selection. */
bool DrawImportTypeOption(const char *label, bool isSelected, bool enabled) {
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    const bool selected = ImGui::Selectable(label, isSelected);
    if (!enabled) {
        ImGui::EndDisabled();
    }
    if (isSelected) {
        ImGui::SetItemDefaultFocus();
    }
    return enabled && selected;
}

/** @brief Draws the content-location input row for import settings. */
void DrawSettingsContentLocation(ImportAssetDraft &draft) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Content Location");
    ImGui::TableNextColumn();

    auto locBuf = MakeInputBuffer<512>(draft.settings.contentLocation);
    ImGui::SetNextItemWidth(-140.0f);
    if (ImGui::InputText("##ContentLocation", locBuf.data(), locBuf.size())) {
        draft.settings.contentLocation = locBuf.data();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse##ContentLocation")) {
        // Could open folder picker here
    }
}

/** @brief Draws the asset-name input row for import settings. */
void DrawSettingsAssetName(ImportAssetDraft &draft, bool &assetIdAutoDerived) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Asset Name");
    ImGui::TableNextColumn();

    auto nameBuf = MakeInputBuffer<256>(draft.assetId);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText("##AssetName", nameBuf.data(), nameBuf.size())) {
        draft.assetId = nameBuf.data();
        assetIdAutoDerived = false;
    }
}

/** @brief Draws the render-scale triplet row for import settings. */
void DrawSettingsScale(ImportAssetDraft &draft, bool &renderScaleAutoDerived) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Scale");
    ImGui::TableNextColumn();

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
#ifdef _WIN32
    sscanf_s(draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#else
    sscanf(draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#endif
    ImGui::SetNextItemWidth(70);
    if (auto scaleXBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleX));
        ImGui::InputText("##ScaleX", scaleXBuf.data(), scaleXBuf.size())) {
        scaleX = std::strtof(scaleXBuf.data(), nullptr);
        draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    if (auto scaleYBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleY));
        ImGui::InputText("##ScaleY", scaleYBuf.data(), scaleYBuf.size())) {
        scaleY = std::strtof(scaleYBuf.data(), nullptr);
        draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    if (auto scaleZBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleZ));
        ImGui::InputText("##ScaleZ", scaleZBuf.data(), scaleZBuf.size())) {
        scaleZ = std::strtof(scaleZBuf.data(), nullptr);
        draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
    }
    ImGui::SameLine();
    if (renderScaleAutoDerived) {
        ImGui::TextDisabled("(auto)");
    } else if (ImGui::SmallButton("Reset##ResetScale")) {
        draft.renderScale = "1.0000,1.0000,1.0000";
        renderScaleAutoDerived = true;
    }
}

} // namespace

/** @copydoc GetTextureSlotLabel */
const char *GetTextureSlotLabel(TextureSlotKind slot) {
    using enum TextureSlotKind;
    switch (slot) {
    case Albedo: return "Albedo";
    case Normal: return "Normal";
    case MetallicRoughness: return "Metallic+Roughness";
    case Emissive: return "Emissive";
    case Occlusion: return "Occlusion";
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
    if (const auto &theme = Ui::GetEditorTheme(); Ui::RenderModalTitleBar(theme, "Import Asset")) {
        Close();
    }

    // Capture modal rect for drag-and-drop hit testing
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const auto windowMax = ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
    m_modalMinX = windowMin.x;
    m_modalMinY = windowMin.y;
    m_modalMaxX = windowMax.x;
    m_modalMaxY = windowMax.y;

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
        const auto labelPos = ImVec2(
            tileMin.x + (availW - labelSz.x) * 0.5f,
            tileMin.y + (dropZoneH - labelSz.y) * 0.5f);
        const ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f));
        dl->AddText(labelPos, labelCol, labelText.c_str());
    }

    ImGui::Spacing();

    // File browser: nav/search fixed, grid scrolls internally
    if (m_fileBrowser.Draw(gridH) && m_fileBrowser.HasSelection()) {
        m_draft.sourcePath = m_fileBrowser.GetSelectedFilePath();
        RefreshImporterFromExtension();
        RefreshIdentitiesFromPath();
        RefreshTextureSlotsFromFbxPreview();
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
void EditorImportAssetModal::DrawSelectedFileInfo() const {
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
    if (std::error_code ec; std::filesystem::is_regular_file(filePath, ec))
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
    if (ImGui::BeginTable("##ImportSettingsForm", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kLabelColW);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);

        DrawSettingsImportAs();
        DrawSettingsContentLocation(m_draft);
        DrawSettingsAssetName(m_draft, m_assetIdAutoDerived);
        DrawSettingsScale(m_draft, m_renderScaleAutoDerived);
        DrawSettingsCheckboxes();
        DrawSettingsNormalMethod();
        DrawSettingsMaterialMethod();
        DrawSettingsAdvanced();

        ImGui::EndTable();
    }
}

void EditorImportAssetModal::DrawSettingsImportAs() {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Import As");
    ImGui::TableNextColumn();

    constexpr std::array<const char*, 3> importTypes = {"Static Mesh", "Skeletal Mesh", "Texture"};
    const bool isSkeletal = m_draft.settings.importType == "skeletal_mesh";
    const bool isTexture = m_draft.settings.importType == "texture";
    int currentIdx = 0;
    if (isSkeletal) currentIdx = 1;
    else if (isTexture) currentIdx = 2;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##ImportType", importTypes[static_cast<size_t>(currentIdx)])) {
        for (int i = 0; i < 3; i++) {
            const bool isSelected = (i == currentIdx);
            if (DrawImportTypeOption(importTypes[static_cast<size_t>(i)], isSelected,
                                     i == 0)) {
                m_draft.settings.importType = "static_mesh";
            }
        }
        ImGui::EndCombo();
    }
}

void EditorImportAssetModal::DrawSettingsCheckboxes() {
    const auto& theme = Ui::GetEditorTheme();

    auto row = [&](const char* label, const char* id, bool& val, bool disabled = false) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableNextColumn();
        if (disabled) ImGui::BeginDisabled();
        Ui::RenderEditorCheckbox(theme, id, val);
        if (disabled) ImGui::EndDisabled();
    };

    row("Auto Generate Collision", "##AutoCollision", m_draft.settings.autoGenerateCollision);
    row("Import Materials", "##ImportMaterials", m_draft.settings.importMaterials);
    row("Import Textures", "##ImportTextures", m_draft.settings.importTextures);
    row("Combine Meshes", "##CombineMeshes", m_draft.settings.combineMeshes, true);
    row("Transform Vertex to Absolute", "##TransformVertex", m_draft.settings.transformVertexToAbsolute);
}

void EditorImportAssetModal::DrawSettingsNormalMethod() {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Normal Import Method");
    ImGui::TableNextColumn();

    constexpr std::array<const char*, 3> normalMethods = {"Import Normals", "Compute Normals", "None"};
    constexpr std::array<const char*, 3> normalKeys = {"import_normals", "compute_normals", "none"};
    int normalIdx = 0;
    for (int i = 0; i < 3; i++) {
        if (m_draft.settings.normalImportMethod == normalKeys[static_cast<size_t>(i)]) {
            normalIdx = i;
            break;
        }
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##NormalMethod", normalMethods[static_cast<size_t>(normalIdx)])) {
        for (int i = 0; i < 3; i++) {
            const bool isSelected = (i == normalIdx);
            if (ImGui::Selectable(normalMethods[static_cast<size_t>(i)], isSelected)) {
                m_draft.settings.normalImportMethod = normalKeys[static_cast<size_t>(i)];
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void EditorImportAssetModal::DrawSettingsMaterialMethod() {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Material Import Method");
    ImGui::TableNextColumn();

    constexpr std::array<const char*, 3> matMethods = {"Create New Materials", "Reuse Existing", "None"};
    constexpr std::array<const char*, 3> matKeys = {"create_new", "reuse_existing", "none"};
    int matIdx = 0;
    for (int i = 0; i < 3; i++) {
        if (m_draft.settings.materialImportMethod == matKeys[static_cast<size_t>(i)]) {
            matIdx = i;
            break;
        }
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##MatMethod", matMethods[static_cast<size_t>(matIdx)])) {
        for (int i = 0; i < 3; i++) {
            const bool isSelected = (i == matIdx);
            if (ImGui::Selectable(matMethods[static_cast<size_t>(i)], isSelected)) {
                m_draft.settings.materialImportMethod = matKeys[static_cast<size_t>(i)];
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void EditorImportAssetModal::DrawSettingsAdvanced() {
    const auto& theme = Ui::GetEditorTheme();
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
    auto meshBuffer = MakeInputBuffer<1024>(m_draft.meshPath);
    ImGui::SetNextItemWidth(250);
    if (ImGui::InputText("##MeshPath", meshBuffer.data(), meshBuffer.size())) {
        m_draft.meshPath.assign(meshBuffer.data());
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
    auto idBuffer = MakeInputBuffer<256>(m_draft.assetId);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Asset ID##ImportId", idBuffer.data(), idBuffer.size())) {
        m_draft.assetId.assign(idBuffer.data());
        m_assetIdAutoDerived = false;
    }

    auto displayBuffer = MakeInputBuffer<256>(m_draft.displayName);
    ImGui::SetNextItemWidth(200);
    if (ImGui::InputText("Display Name##ImportName", displayBuffer.data(),
                          displayBuffer.size())) {
        m_draft.displayName.assign(displayBuffer.data());
        m_displayNameAutoDerived = false;
    }
}

/** @copydoc EditorImportAssetModal::DrawRenderScaleSection */
void EditorImportAssetModal::DrawRenderScaleSection() {
    ImGui::Spacing();
    ImGui::TextUnformatted("Render scale");
    ImGui::SameLine();
    ImGui::TextDisabled("(X, Y, Z)");

    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float scaleZ = 1.0f;
    if (m_renderScaleAutoDerived && !m_draft.sourcePath.empty()) {
        m_draft.renderScale = "1.0000,1.0000,1.0000";
    }

#ifdef _WIN32
    sscanf_s(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#else
    sscanf(m_draft.renderScale.c_str(), "%f,%f,%f", &scaleX, &scaleY, &scaleZ);
#endif

    ImGui::SetNextItemWidth(80);
    if (auto scaleXBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleX));
        ImGui::InputText("##ScaleX", scaleXBuf.data(), scaleXBuf.size())) {
        scaleX = std::strtof(scaleXBuf.data(), nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (auto scaleYBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleY));
        ImGui::InputText("##ScaleY", scaleYBuf.data(), scaleYBuf.size())) {
        scaleY = std::strtof(scaleYBuf.data(), nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80);
    if (auto scaleZBuf = MakeInputBuffer<32>(std::format("{:.4f}", scaleZ));
        ImGui::InputText("##ScaleZ", scaleZBuf.data(), scaleZBuf.size())) {
        scaleZ = std::strtof(scaleZBuf.data(), nullptr);
        m_draft.renderScale = std::format("{:.4f},{:.4f},{:.4f}", scaleX, scaleY, scaleZ);
        m_renderScaleAutoDerived = false;
    }
    ImGui::SameLine();
    if (m_renderScaleAutoDerived) {
        ImGui::TextDisabled("(auto)");
    } else if (ImGui::SmallButton("Reset##ResetScale")) {
        m_draft.renderScale = "1.0000,1.0000,1.0000";
        m_renderScaleAutoDerived = true;
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
void EditorImportAssetModal::DrawTextureSlotRow(TextureSlotDraft &slot) const {
    ImGui::PushID(static_cast<int>(slot.slot));

    const char *label = GetTextureSlotLabel(slot.slot);
    ImGui::TextUnformatted(label);

    ImGui::SetNextItemWidth(200);
    std::string displayPath;
    if (slot.userOverride || !slot.path.empty()) {
        displayPath = slot.path;
    } else if (slot.hasAutoDetected) {
        displayPath = slot.autoDetectedPath;
    }
    auto pathBuffer = MakeInputBuffer<1024>(displayPath);

    const bool isPlaceholder = slot.hasAutoDetected && !slot.userOverride && slot.path.empty();
    if (isPlaceholder) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    }

    if (ImGui::InputText("##TexturePath", pathBuffer.data(), pathBuffer.size())) {
        slot.path.assign(pathBuffer.data());
        slot.userOverride = pathBuffer[0] == '\0';
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
        using enum AssetDiagnosticSeverity;
        ImVec4 color;
        switch (diag.severity) {
        case Info:
            color = ImVec4(0.5f, 0.7f, 1.0f, 1.0f);
            break;
        case Warning:
            color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
            break;
        case Error:
            color = ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
            break;
        default:
            color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
        const char* severityTag = "ERR";
        if (diag.severity == Info) severityTag = "INFO";
        else if (diag.severity == Warning) severityTag = "WARN";
        ImGui::TextColored(color, "[%s] %s", severityTag, diag.message.c_str());
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
    if (const std::string ext = Horo::ToLowerAscii(srcPath.extension().string());
        ext != ".fbx")
        return;

    // Load FBX to extract texture records
    const FbxLoader::FbxLoadResult result =
        FbxLoader::LoadStaticMesh(m_draft.sourcePath);
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
        if (!tex.absolutePath.empty())
            slot.autoDetectedPath = tex.absolutePath;
        else if (!tex.relativePath.empty())
            slot.autoDetectedPath = tex.relativePath;
        else
            slot.autoDetectedPath = tex.filename;
        slot.hasAutoDetected = true;
        slot.enabled = true;
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
            using enum TextureSlotKind;
            switch (slot.slot) {
            case Albedo: overrides.albedoMap = slot.path; break;
            case Normal: overrides.normalMap = slot.path; break;
            case MetallicRoughness: overrides.metallicRoughnessMap = slot.path; break;
            case Emissive: overrides.emissiveMap = slot.path; break;
            case Occlusion: overrides.occlusionMap = slot.path; break;
            case Count: break;
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
        const bool hasErrorsOrWarnings = std::ranges::any_of(outcome.diagnostics, [](const auto& d) {
            return d.severity == AssetDiagnosticSeverity::Error || d.severity == AssetDiagnosticSeverity::Warning;
        });
        if (!hasErrorsOrWarnings)
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
