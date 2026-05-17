/** @file EditorFileBrowser.cpp
 *  @brief Implements the file browser component with thumbnail grid for the Import Asset modal. */
#include "ui/editor/components/EditorFileBrowser.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>

#include <imgui.h>

#include "core/Logger.h"
#include "core/StringUtils.h"
#include "ui/IconsFontAwesome6.h"
#include "ui/UiComponents.h"
#include "ui/editor/components/EditorAssetThumbnailPreview.h"
#include "renderer/Renderer.h"
#include "renderer/ITexture.h"
#include "renderer/Texture.h"

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
    const RenderBackendCapabilities caps = Renderer::GetBackendCapabilities();
    if (!caps.supportsNativeTextureHandles) return 0;

    auto tex = Renderer::CreateTextureFromFile(filePath);
    if (!tex) return 0;

    const RenderTargetHandle handle = tex->GetRenderTargetHandle(true);
    return ToImTextureId(handle);
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
    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const ImVec2 windowMax = ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
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
    constexpr float kPadding = 10.0f;
    if (x < m_modalMinX - kPadding || x > m_modalMaxX + kPadding ||
        y < m_modalMinY - kPadding || y > m_modalMaxY + kPadding)
        return false;
    std::error_code ec;
    std::filesystem::path filePath = std::filesystem::weakly_canonical(path, ec);
    if (ec || !std::filesystem::exists(filePath, ec)) return false;
    if (std::filesystem::is_directory(filePath, ec)) { NavigateTo(filePath); return true; }
    NavigateTo(filePath.parent_path());
    for (auto& entry : m_state.entries) {
        if (entry.fullPath == filePath.string()) { SelectEntry(entry); return true; }
    }
    RefreshEntries();
    for (auto& entry : m_state.entries) {
        if (entry.fullPath == filePath.string()) { SelectEntry(entry); return true; }
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
    if (m_state.searchQuery.empty()) {
        m_state.filteredEntries = m_state.entries;
    } else {
        std::string queryLower = Horo::ToLowerAscii(m_state.searchQuery);
        m_state.filteredEntries.clear();
        for (const auto& entry : m_state.entries) {
            if (Horo::ToLowerAscii(entry.name).find(queryLower) != std::string::npos)
                m_state.filteredEntries.push_back(entry);
        }
    }
    m_state.hasSelection = false;
    for (auto& entry : m_state.filteredEntries) {
        entry.isSelected = (entry.fullPath == m_state.selectedFilePath);
        if (entry.isSelected && !entry.isDirectory) m_state.hasSelection = true;
    }
}

void EditorFileBrowser::DrawNavBar() {
    // Navigation buttons row
    const bool canGoUp = m_state.currentDir.has_parent_path() && !m_state.currentDir.parent_path().empty();
    if (!canGoUp) ImGui::BeginDisabled();
    if (ImGui::SmallButton(ICON_FA_ARROW_LEFT)) NavigateUp();
    if (!canGoUp) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_FA_ROTATE)) Refresh();
    ImGui::SameLine();

    // Breadcrumb: clickable path segments showing full OS path
    const std::string pathStr = m_state.currentDir.generic_string();
    // Split path into segments
    std::vector<std::string> segments;
    std::string accumulated;
    std::string current;
    for (size_t i = 0; i < pathStr.size(); ++i) {
        char c = pathStr[i];
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                accumulated += current + "/";
                segments.push_back(accumulated);
            } else if (accumulated.empty()) {
                // Leading slash
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

    // Draw breadcrumb segments with angle-right separators
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0, 4.0f);
            const ImVec2 sepSz = Ui::GetIconSize(ICON_FA_ANGLE_RIGHT, Ui::GetIconSize(Ui::GetEditorTheme()));
            const ImVec2 sepPos = ImVec2(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y + (ImGui::GetTextLineHeight() - sepSz.y) * 0.5f);
            Ui::DrawIcon(ImGui::GetWindowDrawList(), ICON_FA_ANGLE_RIGHT, ImVec2(sepPos.x + sepSz.x * 0.5f, sepPos.y + sepSz.y * 0.5f), ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.6f)), Ui::GetIconSize(Ui::GetEditorTheme()));
            ImGui::SameLine(0, 4.0f);
        }
        const std::string& seg = segments[i];
        // Extract just the folder name for display
        std::string displayName = seg;
        if (!displayName.empty() && displayName.back() == '/')
            displayName.pop_back();
        size_t lastSlash = displayName.find_last_of('/');
        if (lastSlash != std::string::npos)
            displayName = displayName.substr(lastSlash + 1);
        if (displayName.empty())
            displayName = "/";

        // Make clickable
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(displayName.c_str())) {
            // Navigate to this segment's path
            std::string targetPath = seg;
            if (!targetPath.empty() && targetPath.back() == '/')
                targetPath.pop_back();
            NavigateTo(std::filesystem::path(targetPath));
        }
        ImGui::PopID();
    }
}

void EditorFileBrowser::DrawSearchBar() {
    ImGui::SetNextItemWidth(-FLT_MIN);
    char searchBuf[256]{};
    m_state.searchQuery.copy(searchBuf, sizeof(searchBuf) - 1);
    const std::string searchHint = std::string(ICON_FA_MAGNIFYING_GLASS) + " Search files...";
    if (ImGui::InputTextWithHint("##FileBrowserSearch", searchHint.c_str(),
                                  searchBuf, sizeof(searchBuf))) {
        m_state.searchQuery = searchBuf;
        ApplySearchFilter();
    }
}

void EditorFileBrowser::DrawDropZone() {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float dropZoneH = 50.0f;
    ImGui::BeginChild("##DropZone", ImVec2(availW, dropZoneH), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 tileMin = ImGui::GetWindowPos();
    const ImVec2 tileMax = ImVec2(tileMin.x + availW, tileMin.y + dropZoneH);
    m_dropZoneMinY = tileMin.y;
    m_dropZoneMaxY = tileMax.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool isHovered = ImGui::IsWindowHovered();
    const ImVec4 bgColor = isHovered ? ImVec4(0.14f, 0.20f, 0.30f, 0.95f) : ImVec4(0.09f, 0.12f, 0.18f, 0.85f);
    dl->AddRectFilled(tileMin, tileMax, ImGui::ColorConvertFloat4ToU32(bgColor), 4.0f);
    const ImVec4 borderColor = isHovered ? ImVec4(0.45f, 0.62f, 0.90f, 0.90f) : ImVec4(0.35f, 0.50f, 0.70f, 0.50f);
    dl->AddRect(tileMin, tileMax, ImGui::ColorConvertFloat4ToU32(borderColor), 4.0f, 0, 1.0f);
        const std::string labelText = std::string(ICON_FA_PLUS) + " Drag & drop files here";
        const ImVec2 labelSz = ImGui::CalcTextSize(labelText.c_str());
        const ImVec2 labelPos = ImVec2(tileMin.x + (availW - labelSz.x) * 0.5f, tileMin.y + (dropZoneH - labelSz.y) * 0.5f);
        const ImU32 labelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f));
        dl->AddText(labelPos, labelCol, labelText.c_str());
    ImGui::EndChild();
}

void EditorFileBrowser::DrawThumbnailGrid() {
    const float availW = ImGui::GetContentRegionAvail().x;
    const float spacing = kTileSpacing;
    const float minTileW = 140.0f; // Larger thumbnails for better readability
    const int columns = std::max(1, static_cast<int>((availW + spacing) / (minTileW + spacing)));
    const float tileW = (availW - spacing * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    const float thumbSize = tileW - kThumbPad * 2.0f;
    const float tileH = thumbSize + kThumbPad * 2.0f + 36.0f;
    const float rowHeight = tileH + ImGui::GetStyle().ItemSpacing.y;

    const int totalEntries = static_cast<int>(m_state.filteredEntries.size());
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
        ImGui::Dummy(ImVec2(0, firstVisibleRow * rowHeight));

    const int firstVisibleIndex = firstVisibleRow * columns;
    const int lastVisibleIndex = std::min(totalEntries, lastVisibleRow * columns);
    for (int i = firstVisibleIndex; i < lastVisibleIndex; ++i) {
        const auto& entry = m_state.filteredEntries[static_cast<size_t>(i)];
        const int col = (i - firstVisibleIndex) % columns;
        if (col > 0) ImGui::SameLine(0.0f, spacing);
        ImGui::PushID(entry.fullPath.c_str());
        DrawThumbnailTile(entry, thumbSize, tileH);
        ImGui::PopID();
    }

    const int remainingRows = totalRows - lastVisibleRow;
    if (remainingRows > 0)
        ImGui::Dummy(ImVec2(0, remainingRows * rowHeight));

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
    const ImVec4 bgColor = entry.isSelected ? ImVec4(0.14f, 0.20f, 0.30f, 0.95f)
                             : (hovered ? ImVec4(0.12f, 0.16f, 0.22f, 0.90f) : ImVec4(0.09f, 0.12f, 0.18f, 0.85f));
    const ImVec2 bgMin = tileMin;
    const ImVec2 bgMax = ImVec2(tileMin.x + thumbSize + kThumbPad * 2.0f, tileMin.y + tileH);
    dl->AddRectFilled(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(bgColor), 6.0f);
    const ImVec4 borderColor = entry.isSelected ? ImVec4(0.2f, 0.5f, 1.0f, 1.0f)
                                 : (hovered ? ImVec4(0.45f, 0.62f, 0.90f, 0.90f) : ImVec4(0.35f, 0.50f, 0.70f, 0.50f));
    dl->AddRect(bgMin, bgMax, ImGui::ColorConvertFloat4ToU32(borderColor), 6.0f, 0, entry.isSelected ? 2.0f : 1.0f);
    const ImVec2 thumbMin = ImVec2(tileMin.x + kThumbPad, tileMin.y + kThumbPad);
    const ImVec2 thumbMax = ImVec2(thumbMin.x + thumbSize, thumbMin.y + thumbSize);
    const ImU32 bgThumbCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.10f, 0.13f, 0.18f, 0.95f));
    dl->AddRectFilled(thumbMin, thumbMax, bgThumbCol, 4.0f);

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
        const ImVec2 thumbCenter = ImVec2((thumbMin.x + thumbMax.x) * 0.5f, (thumbMin.y + thumbMax.y) * 0.5f);
        const float iconSize = Ui::GetIconSize(Ui::GetEditorTheme()) * 5.0f;
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
    std::string displayName = entry.name;
    const float maxLabelW = thumbSize;
    if (ImGui::CalcTextSize(displayName.c_str()).x > maxLabelW) {
        while (!displayName.empty() && ImGui::CalcTextSize((displayName + "...").c_str()).x > maxLabelW)
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
        const std::string sizeLabel = entry.itemCount >= 0
            ? std::format("{} items", entry.itemCount)
            : std::string("-- items");
        const ImVec2 sizeSz = ImGui::CalcTextSize(sizeLabel.c_str());
        const ImVec2 sizePos = ImVec2(
            tileMin.x + std::max(4.0f, (thumbSize + kThumbPad * 2.0f - sizeSz.x) * 0.5f),
            nameY + ImGui::GetTextLineHeight() + 2.0f);
        dl->AddText(sizePos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f)), sizeLabel.c_str());
    } else {
        const std::string sizeLabel = Horo::FormatFileSize(entry.sizeBytes);
        const ImVec2 sizeSz = ImGui::CalcTextSize(sizeLabel.c_str());
        const ImVec2 sizePos = ImVec2(
            tileMin.x + std::max(4.0f, (thumbSize + kThumbPad * 2.0f - sizeSz.x) * 0.5f),
            nameY + ImGui::GetTextLineHeight() + 2.0f);
        dl->AddText(sizePos, ImGui::ColorConvertFloat4ToU32(ImVec4(0.55f, 0.62f, 0.72f, 0.85f)), sizeLabel.c_str());
    }
    if (clicked) {
        if (entry.isDirectory) m_pendingNavigate = entry.fullPath;
        else SelectEntry(entry);
    }
    ImGui::EndChild();
}

void EditorFileBrowser::DrawStatusBar() {
    int fileCount = 0, dirCount = 0;
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
