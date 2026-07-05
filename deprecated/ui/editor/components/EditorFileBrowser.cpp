/** @file EditorFileBrowser.cpp
 *  @brief Implements the file browser component with thumbnail grid for the Import Asset modal. */
#include "ui/editor/components/EditorFileBrowser.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>

#include <imgui.h>

#include "core/Logger.h"
#include "core/StringUtils.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"
#include "renderer/Renderer.h"
#include "renderer/ITexture.h"

namespace Horo::Editor {

ImFont* EditorFileBrowser::s_largeIconFont = nullptr;

/** @copydoc EditorFileBrowser::SetLargeIconFont */
void EditorFileBrowser::SetLargeIconFont(ImFont* font) {
    s_largeIconFont = font;
}

namespace {
bool IsMeshExtension(std::string_view ext) {
    return ext == ".fbx" || ext == ".obj" || ext == ".mesh.bin" ||
           ext == ".skinned.bin" || ext == ".skinned" || ext == ".gltf" || ext == ".glb";
}
bool IsTextureExtension(std::string_view ext) {
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
           ext == ".bmp" || ext == ".tga" || ext == ".webp" || ext == ".hdr";
}

bool IsSceneExtension(std::string_view ext) {
    return ext == ".json" || ext == ".scene" || ext == ".prefab";
}

bool IsScriptExtension(std::string_view ext) {
    return ext == ".lua" || ext == ".py" || ext == ".cs";
}

bool IsShaderExtension(std::string_view ext) {
    return ext == ".vert" || ext == ".frag" || ext == ".glsl" ||
           ext == ".hlsl" || ext == ".shader" || ext == ".spv";
}

bool IsCodeExtension(std::string_view ext) {
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" ||
           ext == ".c" || ext == ".h" || ext == ".hpp" || ext == ".hh";
}

const char* FileTypeFilterLabel(FileBrowserTypeFilter filter) {
    using enum FileBrowserTypeFilter;
    switch (filter) {
        case Folders:  return "Folders";
        case Meshes:   return "Meshes";
        case Textures: return "Textures";
        case Scenes:   return "Scenes";
        case Scripts:  return "Scripts";
        case Shaders:  return "Shaders";
        case Code:     return "Code";
        case Count:    break;
    }
    return "";
}

bool MatchesTypeFilter(const FileBrowserEntry& entry,
                       const std::array<bool, kFileBrowserTypeFilterCount>& filters) {
    const auto isSelected = [&filters](FileBrowserTypeFilter filter) {
        return filters[static_cast<size_t>(filter)];
    };

    bool hasActiveFilter = false;
    for (const bool selected : filters) {
        if (selected) {
            hasActiveFilter = true;
            break;
        }
    }
    if (!hasActiveFilter)
        return true;
    if (entry.isDirectory)
        return true;

    return (isSelected(FileBrowserTypeFilter::Meshes) &&
            IsMeshExtension(entry.extension)) ||
           (isSelected(FileBrowserTypeFilter::Textures) &&
            IsTextureExtension(entry.extension)) ||
           (isSelected(FileBrowserTypeFilter::Scenes) &&
            IsSceneExtension(entry.extension)) ||
           (isSelected(FileBrowserTypeFilter::Scripts) &&
            IsScriptExtension(entry.extension)) ||
           (isSelected(FileBrowserTypeFilter::Shaders) &&
            IsShaderExtension(entry.extension)) ||
           (isSelected(FileBrowserTypeFilter::Code) &&
            IsCodeExtension(entry.extension));
}

/** @brief Tries to load a rendered mesh preview texture for the given file path. */
ImTextureID TryLoadMeshPreviewTexture(const std::string& filePath) {
    const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
    if (!caps.supportsNativeTextureHandles) {
        LogWarn("[FileBrowser] Backend does not support native texture handles");
        return 0;
    }
    if (!caps.supportsOffscreenTargets) {
        LogWarn("[FileBrowser] Backend does not support offscreen targets");
        return 0;
    }

    // Build a minimal AssetDef from the file path
    AssetDef asset;
    asset.mesh = filePath;

    RenderTargetHandle handle;
    if (!TryGetAssetPreviewHandle("filebrowser_preview", asset, &handle)) {
        LogWarn("[FileBrowser] TryGetAssetPreviewHandle failed for {}", filePath);
        return 0;
    }

    LogInfo("[FileBrowser] Preview loaded for {}", filePath);
    return ToImTextureId(handle);
}

/** @brief Loads an image file as a thumbnail texture for supported formats (PNG, JPG, etc.). */
ImTextureID TryLoadTexturePreview(const std::string& filePath) {
    if (const auto caps = Renderer::GetBackendCapabilities(); !caps.supportsNativeTextureHandles) return 0;

    auto tex = Renderer::CreateTextureFromFile(filePath);
    if (!tex) return 0;

    const RenderTargetHandle handle = tex->GetRenderTargetHandle(true);
    return ToImTextureId(handle);
}

std::vector<std::string> SplitPathIntoBreadcrumbSegments(const std::string& pathStr) {
    std::vector<std::string> segments;
    std::string accumulated;
    std::string current;
    for (const char c : pathStr) {
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                accumulated += current + "/";
                segments.push_back(accumulated);
            } else if (accumulated.empty()) {
                accumulated = "/";
                segments.push_back(accumulated);
            }
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        accumulated += current;
        segments.push_back(accumulated);
    }
    return segments;
}

std::optional<std::string> DrawBreadcrumbs(const Ui::EditorTheme& theme, const std::string& pathStr) {
    std::vector<std::string> segments = SplitPathIntoBreadcrumbSegments(pathStr);
    std::optional<std::string> navigateTarget;
    
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0, 4.0f);
            const auto sepSz = Ui::GetIconSize(ICON_FA_ANGLE_RIGHT, Ui::GetIconSize(theme));
            const auto sepPos = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + (ImGui::GetTextLineHeight() - sepSz.y) * 0.5f);
            Ui::DrawIcon(ImGui::GetWindowDrawList(), ICON_FA_ANGLE_RIGHT, ImVec2(sepPos.x + sepSz.x * 0.5f, sepPos.y + sepSz.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f)), Ui::GetIconSize(theme));
            ImGui::SameLine(0, 4.0f);
        }
        
        const auto& seg = segments[i];
        auto displayName = seg;
        if (!displayName.empty() && displayName.back() == '/')
            displayName.pop_back();
            
        if (const size_t lastSlash = displayName.find_last_of('/');
            lastSlash != std::string::npos)
            displayName = displayName.substr(lastSlash + 1);
        if (displayName.empty())
            displayName = "/";

        ImGui::PushID(static_cast<int>(i));
        if (Ui::Button(theme, Ui::ButtonStyleVariant::Secondary, displayName.c_str())) {
            std::string targetPath = seg;
            if (!targetPath.empty() && targetPath.back() == '/')
                targetPath.pop_back();
            navigateTarget = targetPath;
        }
        ImGui::PopID();
    }
    return navigateTarget;
}
} // namespace

void EditorFileBrowser::SetRootPath(const std::filesystem::path& root) {
    m_state.rootDir = root;
    if (m_state.currentDir.empty()) m_state.currentDir = root;
}

void EditorFileBrowser::NavigateTo(const std::filesystem::path& dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) {
        LogWarn("FileBrowser: cannot navigate to non-directory: {}", dir.string());
        return;
    }
    std::filesystem::path canonical = std::filesystem::weakly_canonical(dir, ec);
    if (ec) {
        LogWarn("FileBrowser: weakly_canonical failed for: {}", dir.string());
        return;
    }
    // No root boundary — allow navigating anywhere on OS
    m_state.currentDir = canonical;
    m_state.searchQuery.clear();
    RefreshEntries();
    ClearSelection();
}

void EditorFileBrowser::NavigateUp() {
    std::filesystem::path parent = m_state.currentDir.parent_path();
    if (!parent.empty())
        NavigateTo(parent);
}

void EditorFileBrowser::Refresh() { RefreshEntries(); }

bool EditorFileBrowser::Draw(float gridHeight) {
    const std::string previousSelection = m_state.selectedFilePath;
    const auto windowMin = ImGui::GetWindowPos();
    const auto windowSize = ImGui::GetWindowSize();
    const auto windowMax = ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
    SetModalRect(windowMin.x, windowMin.y, windowMax.x, windowMax.y);
    DrawNavBar();
    ImGui::Spacing();
    DrawSearchBar();
    ImGui::Spacing();
    m_gridMinY = ImGui::GetCursorScreenPos().y;
    if (gridHeight > 0.0f) {
        ImGui::BeginChild("##GridScroll", ImVec2(0, gridHeight));
        DrawThumbnailGrid();
        ImGui::EndChild();
    } else {
        DrawThumbnailGrid();
    }
    m_gridMaxY = ImGui::GetCursorScreenPos().y;
    return m_state.selectedFilePath != previousSelection;
}

bool EditorFileBrowser::HandleFileDrop(float x, float y, const std::string& path) {
    if (constexpr float kPadding = 10.0f;
        x < m_modalMinX - kPadding || x > m_modalMaxX + kPadding ||
        y < m_modalMinY - kPadding || y > m_modalMaxY + kPadding)
        return false;
    std::error_code ec;
    const std::filesystem::path filePath = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(filePath, ec)) return false;
    if (std::filesystem::is_directory(filePath, ec)) { NavigateTo(filePath); return true; }
    NavigateTo(filePath.parent_path());
    const std::string filePathString = filePath.generic_string();
    if (const auto found = std::ranges::find(m_state.entries, filePathString,
                                             &FileBrowserEntry::fullPath);
        found != m_state.entries.end()) {
        SelectEntry(*found);
        return true;
    }
    RefreshEntries();
    if (const auto found = std::ranges::find(m_state.entries, filePathString,
                                             &FileBrowserEntry::fullPath);
        found != m_state.entries.end()) {
        SelectEntry(*found);
        return true;
    }
    return false;
}

void EditorFileBrowser::RefreshEntries() {
    m_state.entries.clear();
    std::error_code ec;
    if (!std::filesystem::is_directory(m_state.currentDir, ec) || ec) return;
    for (const auto& entry : std::filesystem::directory_iterator(m_state.currentDir, ec)) {
        if (ec) break;
        const std::string fileName = entry.path().filename().string();
        // Skip hidden files, macOS resource forks, and entries with empty names
        if (fileName.empty() || fileName.starts_with('.') ||
            fileName.starts_with("._") || fileName == "__MACOSX")
            continue;
        FileBrowserEntry fbEntry;
        fbEntry.name = fileName;
        fbEntry.fullPath = entry.path().generic_string();
        fbEntry.isDirectory = entry.is_directory(ec);
        fbEntry.extension = Horo::ToLowerAscii(entry.path().extension().string());
        // Resolve compound extensions: model.fbx.bin → extension stored as .fbx
        if (fbEntry.extension == ".bin") {
            const std::string stemExt =
                Horo::ToLowerAscii(entry.path().stem().extension().string());
            if (!stemExt.empty()) fbEntry.extension = stemExt;
        }
        if (!fbEntry.isDirectory) fbEntry.sizeBytes = entry.file_size(ec);
        else {
            std::error_code countEc;
            int count = 0;
            for (auto it = std::filesystem::directory_iterator(entry.path(), countEc);
                 it != std::filesystem::directory_iterator() && !countEc; ++it)
                ++count;
            if (!countEc) fbEntry.itemCount = count;
        }
        m_state.entries.push_back(std::move(fbEntry));
    }
    std::ranges::sort(m_state.entries, [](const FileBrowserEntry& a, const FileBrowserEntry& b) {
        if (a.isDirectory != b.isDirectory) return a.isDirectory;
        return a.name < b.name;
    });
    ApplySearchFilter();
}

void EditorFileBrowser::ApplySearchFilter() {
    const std::string queryLower = Horo::ToLowerAscii(m_state.searchQuery);
    m_state.filteredEntries.clear();
    for (const auto& entry : m_state.entries) {
        if (!MatchesTypeFilter(entry, m_state.typeFilters))
            continue;
        if (!queryLower.empty() &&
            Horo::ToLowerAscii(entry.name).find(queryLower) == std::string::npos)
            continue;
        m_state.filteredEntries.push_back(entry);
    }

    m_state.hasSelection = false;
    for (auto& entry : m_state.filteredEntries) {
        entry.isSelected = (entry.fullPath == m_state.selectedFilePath);
        if (entry.isSelected && !entry.isDirectory) m_state.hasSelection = true;
    }
}

void EditorFileBrowser::DrawNavBar() {
    const auto& theme = Ui::GetEditorTheme();

    // Navigation buttons row
    const bool canGoUp = m_state.currentDir.has_parent_path() && !m_state.currentDir.parent_path().empty();
    if (!canGoUp) ImGui::BeginDisabled();
    if (Ui::Button(theme, Ui::ButtonStyleVariant::Secondary,
                   ICON_FA_ARROW_LEFT)) NavigateUp();
    if (!canGoUp) ImGui::EndDisabled();
    ImGui::SameLine();
    if (Ui::Button(theme, Ui::ButtonStyleVariant::Secondary,
                   ICON_FA_ROTATE)) Refresh();
    ImGui::SameLine();

    if (auto target = DrawBreadcrumbs(theme, m_state.currentDir.generic_string())) {
        NavigateTo(std::filesystem::path(*target));
    }
}

void EditorFileBrowser::DrawSearchBar() {
    const auto& theme = Ui::GetEditorTheme();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float filterW = 156.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const bool inlineFilter = availW > filterW + 160.0f;
    const float searchW = inlineFilter ? availW - filterW - spacing : availW;

    ImGui::SetNextItemWidth(searchW);
    std::array<char, 256> searchBuf{};
    m_state.searchQuery.copy(searchBuf.data(), searchBuf.size() - 1);
    if (Ui::InputTextWithLeadingIcon(theme, "##FileBrowserSearch",
                                     ICON_FA_MAGNIFYING_GLASS,
                                     "Search files...", searchBuf.data(),
                                     searchBuf.size())) {
        m_state.searchQuery = searchBuf.data();
        ApplySearchFilter();
    }

    if (inlineFilter)
        ImGui::SameLine(0.0f, spacing);
    ImGui::SetNextItemWidth(inlineFilter ? filterW : -FLT_MIN);
    using enum FileBrowserTypeFilter;
    auto filters = std::array<Ui::MultiSelectDropdownItem, kFileBrowserTypeFilterCount>{{
        {FileTypeFilterLabel(Folders),
         &m_state.typeFilters[static_cast<size_t>(Folders)]},
        {FileTypeFilterLabel(Meshes),
         &m_state.typeFilters[static_cast<size_t>(Meshes)]},
        {FileTypeFilterLabel(Textures),
         &m_state.typeFilters[static_cast<size_t>(Textures)]},
        {FileTypeFilterLabel(Scenes),
         &m_state.typeFilters[static_cast<size_t>(Scenes)]},
        {FileTypeFilterLabel(Scripts),
         &m_state.typeFilters[static_cast<size_t>(Scripts)]},
        {FileTypeFilterLabel(Shaders),
         &m_state.typeFilters[static_cast<size_t>(Shaders)]},
        {FileTypeFilterLabel(Code),
         &m_state.typeFilters[static_cast<size_t>(Code)]},
    }};
    if (Ui::MultiSelectDropdown(theme, "##FileBrowserTypeFilter", filters,
                                "All Files"))
        ApplySearchFilter();
}

void EditorFileBrowser::DrawDropZone() {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float dropZoneH = 50.0f;
    ImGui::BeginChild("##DropZone", ImVec2(availW, dropZoneH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto tileMin = ImGui::GetWindowPos();
    const auto tileMax = ImVec2(tileMin.x + availW, tileMin.y + dropZoneH);
    m_dropZoneMinY = tileMin.y;
    m_dropZoneMaxY = tileMax.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool isHovered = ImGui::IsWindowHovered();
    const ImVec4 bgColor = isHovered ? ImVec4(0.14f, 0.20f, 0.30f, 0.95f) : ImVec4(0.09f, 0.12f, 0.18f, 0.85f);
    dl->AddRectFilled(tileMin, tileMax, ImGui::ColorConvertFloat4ToU32(bgColor), 4.0f);
    const ImVec4 borderColor = isHovered ? ImVec4(0.45f, 0.62f, 0.90f, 0.90f) : ImVec4(0.35f, 0.50f, 0.70f, 0.50f);
    dl->AddRect(tileMin, tileMax, ImGui::ColorConvertFloat4ToU32(borderColor), 4.0f, 0, 1.0f);
        const auto labelText = std::string(ICON_FA_PLUS) + " Drag & drop files here";
        const auto labelSz = ImGui::CalcTextSize(labelText.c_str());
        const auto labelPos = ImVec2(tileMin.x + (availW - labelSz.x) * 0.5f, tileMin.y + (dropZoneH - labelSz.y) * 0.5f);
        const auto labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f));
        dl->AddText(labelPos, labelCol, labelText.c_str());
    ImGui::EndChild();
}

void EditorFileBrowser::DrawThumbnailGrid() {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float spacing = kTileSpacing;
    const float minTileW = 111.0f;
    const int columns = std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
    const float tileW = (availW - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float thumbSize = tileW - kThumbPad * 2.0f;
    const float tileH = thumbSize + kThumbPad * 2.0f + 34.0f;
    const float rowHeight = tileH + ImGui::GetStyle().ItemSpacing.y;

    const auto totalEntries = static_cast<int>(m_state.filteredEntries.size());
    if (totalEntries == 0) {
        ImGui::TextDisabled("No files found");
        return;
    }

    // Only process visible rows for performance with large directories
    const int totalRows = (totalEntries + columns - 1) / columns;
    const float scrollY = ImGui::GetScrollY();
    const int firstVisibleRow = std::max(0, static_cast<int>(scrollY / rowHeight));
    const int visibleRows = static_cast<int>(std::ceil(ImGui::GetWindowHeight() / rowHeight)) + 1;
    const int lastVisibleRow = std::min(totalRows, firstVisibleRow + visibleRows);

    if (firstVisibleRow > 0)
        ImGui::Dummy(ImVec2(0, static_cast<float>(firstVisibleRow) * rowHeight));

    const int firstVisibleIndex = firstVisibleRow * columns;
    const int lastVisibleIndex = std::min(totalEntries, lastVisibleRow * columns);
    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        const auto& entry = m_state.filteredEntries[static_cast<size_t>(i)];
        if (const int col = (i - firstVisibleIndex) % columns; col > 0)
            ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(entry.fullPath.c_str());
        DrawThumbnailTile(entry, thumbSize, tileH);
        ImGui::PopID();
    }

    if (const int remainingRows = totalRows - lastVisibleRow; remainingRows > 0)
        ImGui::Dummy(ImVec2(0, static_cast<float>(remainingRows) * rowHeight));

    if (m_pendingNavigate) {
        NavigateTo(*m_pendingNavigate);
        m_pendingNavigate.reset();
    }
}

void EditorFileBrowser::DrawThumbnailTile(const FileBrowserEntry& entry, float thumbSize, float tileH) {
    ImGui::BeginChild("##tile", ImVec2(thumbSize + kThumbPad * 2.0f, tileH),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 tileMin = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##tile_btn", ImVec2(thumbSize + kThumbPad * 2.0f - 2.0f, tileH - 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    ImVec4 bgColor;
    if (entry.isSelected) bgColor = ImVec4(0.14f, 0.20f, 0.30f, 0.95f);
    else if (hovered) bgColor = ImVec4(0.12f, 0.16f, 0.22f, 0.90f);
    else bgColor = ImVec4(0.09f, 0.12f, 0.18f, 0.85f);
    
    const auto bgMin = tileMin;
    const auto bgMax = ImVec2(tileMin.x + thumbSize + kThumbPad * 2.0f, tileMin.y + tileH);
    dl->AddRectFilled(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(bgColor), 6.0f);
    
    ImVec4 borderColor;
    if (entry.isSelected) borderColor = ImVec4(0.2f, 0.5f, 1.0f, 1.0f);
    else if (hovered) borderColor = ImVec4(0.45f, 0.62f, 0.90f, 0.90f);
    else borderColor = ImVec4(0.35f, 0.50f, 0.70f, 0.50f);
    
    dl->AddRect(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(borderColor), 6.0f, 0, entry.isSelected ? 2.0f : 1.0f);
    const auto thumbMin = ImVec2(tileMin.x + kThumbPad, tileMin.y + kThumbPad);
    const auto thumbMax = ImVec2(thumbMin.x + thumbSize, thumbMin.y + thumbSize);
    const auto bgThumbCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.13f, 0.18f, 0.95f));
    dl->AddRectFilled(thumbMin, thumbMax, bgThumbCol, 4.0f);

    DrawThumbnailTileIcon(dl, entry, thumbMin, thumbMax);
    DrawThumbnailTileLabel(dl, entry, thumbSize, tileMin, thumbMax);

    if (clicked) {
        if (entry.isDirectory) m_pendingNavigate = entry.fullPath;
        else SelectEntry(entry);
    }
    ImGui::EndChild();
}

void EditorFileBrowser::DrawThumbnailTileIcon(ImDrawList* dl, const FileBrowserEntry& entry, const ImVec2& thumbMin, const ImVec2& thumbMax) {
    // Try to render mesh preview for supported formats
    const bool isMesh = IsMeshExtension(entry.extension);
    const bool isTexture = IsTextureExtension(entry.extension);
    ImTextureID previewTex = 0;
    if (isMesh) {
        auto& cache = m_previewCache[entry.fullPath];
        if (!cache.loaded && !cache.failed) {
            cache.path = entry.fullPath;
            cache.textureId = TryLoadMeshPreviewTexture(entry.fullPath);
            cache.loaded = true;
            if (!cache.textureId) cache.failed = true;
        }
        previewTex = cache.textureId;
    } else if (isTexture) {
        auto& cache = m_texturePreviewCache[entry.fullPath];
        if (!cache.loaded && !cache.failed) {
            cache.path = entry.fullPath;
            cache.textureId = TryLoadTexturePreview(entry.fullPath);
            cache.loaded = true;
            if (!cache.textureId) cache.failed = true;
        }
        previewTex = cache.textureId;
    }

    if (previewTex) {
        // Render the 3D preview texture directly on the draw list
        dl->AddImage(previewTex, thumbMin, thumbMax);
    } else {
        const auto thumbCenter = ImVec2((thumbMin.x + thumbMax.x) * 0.5f, (thumbMin.y + thumbMax.y) * 0.5f);
        const float iconSize = Ui::GetIconSize(Ui::GetEditorTheme()) * 3.75f;
        if (s_largeIconFont) ImGui::PushFont(s_largeIconFont);
        if (entry.isDirectory) {
            const ImU32 folderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.60f, 0.30f, 0.95f));
            Ui::DrawIcon(dl, ICON_FA_FOLDER, thumbCenter, folderCol, iconSize);
        } else if (isMesh) {
            const ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.64f, 0.75f, 0.95f));
            Ui::DrawIcon(dl, ICON_FA_CUBE, thumbCenter, iconCol, iconSize);
        } else if (IsTextureExtension(entry.extension)) {
            const ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.64f, 0.75f, 0.95f));
            Ui::DrawIcon(dl, ICON_FA_FILE, thumbCenter, iconCol, iconSize);
        } else {
            const ImU32 iconCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.64f, 0.75f, 0.95f));
            Ui::DrawIcon(dl, ICON_FA_FILE, thumbCenter, iconCol, iconSize);
        }
        if (s_largeIconFont) ImGui::PopFont();
    }
}

void EditorFileBrowser::DrawThumbnailTileLabel(ImDrawList* dl, const FileBrowserEntry& entry, float thumbSize, const ImVec2& tileMin, const ImVec2& thumbMax) const {
    std::string displayName = entry.name;
    if (const float maxLabelW = thumbSize;
        ImGui::CalcTextSize(displayName.c_str()).x > maxLabelW) {
        while (!displayName.empty() &&
               ImGui::CalcTextSize((displayName + "...").c_str()).x > maxLabelW)
            displayName.pop_back();
        displayName += "...";
    }
    const ImVec2 nameSz = ImGui::CalcTextSize(displayName.c_str());
    const float nameX = tileMin.x + std::max(4.0f, (thumbSize + kThumbPad * 2.0f - nameSz.x) * 0.5f);
    const float nameY = thumbMax.y + 4.0f;
    const ImU32 nameColor = entry.isDirectory
                                ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.70f, 0.60f, 0.30f, 0.95f))
                                : ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImVec2(nameX, nameY), nameColor, displayName.c_str());
    
    if (entry.isDirectory) {
        const auto sizeLabel = entry.itemCount >= 0
            ? std::format("{} items", entry.itemCount)
            : std::string("-- items");
        const auto sizeSz = ImGui::CalcTextSize(sizeLabel.c_str());
        const auto sizePos = ImVec2(
            tileMin.x + std::max(4.0f, (thumbSize + kThumbPad * 2.0f - sizeSz.x) * 0.5f),
            nameY + ImGui::GetTextLineHeight() + 2.0f);
        dl->AddText(sizePos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f)), sizeLabel.c_str());
    } else {
        const auto sizeLabel = Horo::FormatFileSize(entry.sizeBytes);
        const auto sizeSz = ImGui::CalcTextSize(sizeLabel.c_str());
        const auto sizePos = ImVec2(
            tileMin.x + std::max(4.0f, (thumbSize + kThumbPad * 2.0f - sizeSz.x) * 0.5f),
            nameY + ImGui::GetTextLineHeight() + 2.0f);
        dl->AddText(sizePos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f)), sizeLabel.c_str());
    }
}

void EditorFileBrowser::DrawStatusBar() const {
    int fileCount = 0;
    int dirCount = 0;
    for (const auto& entry : m_state.filteredEntries) {
        if (entry.isDirectory) ++dirCount; else ++fileCount;
    }
    std::string status;
    if (m_state.hasSelection) {
        status = "1 file selected";
    } else {
        status = std::format("{} file{}, {} director{}",
                             fileCount, fileCount == 1 ? "" : "s",
                             dirCount, dirCount == 1 ? "y" : "ies");
    }
    ImGui::TextDisabled("%s", status.c_str());
}

void EditorFileBrowser::SelectEntry(const FileBrowserEntry& entry) {
    m_state.selectedFilePath = entry.fullPath;
    m_state.hasSelection = !entry.isDirectory;
    for (auto& e : m_state.filteredEntries) e.isSelected = (e.fullPath == entry.fullPath);
}

void EditorFileBrowser::ClearSelection() {
    m_state.selectedFilePath.clear();
    m_state.hasSelection = false;
    for (auto& e : m_state.filteredEntries) e.isSelected = false;
}

} // namespace Horo::Editor
