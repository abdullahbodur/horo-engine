#include "editor/screens/workspace/panels/global_dock/panes/asset_browser/AssetBrowserCards.h"

#include "Horo/Editor/EditorTheme.h"
#include "editor/renderer/EditorGuiRenderer.h"
#include "editor/screens/workspace/EditorWorkspaceViewModel.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <unordered_set>

namespace Horo::Editor {
    namespace {
        constexpr float CardFooterHeight = 20.0F;
        constexpr float CardRadius = 3.0F;

        enum class AssetGlyph {
            None,
            Folder,
            Mesh,
            Image,
            Audio,
            Generic,
            Prefab,
        };

        struct AssetCardPresentation {
            std::string_view name;
            ImU32 gradientStart;
            ImU32 gradientMid;
            ImU32 gradientEnd;
            AssetGlyph glyph;
        };

        [[nodiscard]] AssetCardPresentation PresentEntry(const ContentBrowserEntry &entry) {
            if (entry.kind == ContentBrowserEntryKind::Directory) {
                return {entry.displayName, IM_COL32(24, 31, 39, 255), IM_COL32(30, 39, 50, 255), IM_COL32(35, 46, 60, 255),
                        AssetGlyph::Folder};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Image) {
                return {entry.displayName, IM_COL32(26, 21, 32, 255), IM_COL32(30, 23, 38, 255), IM_COL32(34, 25, 44, 255),
                        AssetGlyph::Image};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Audio) {
                return {entry.displayName, IM_COL32(28, 26, 18, 255), IM_COL32(33, 31, 22, 255), IM_COL32(38, 35, 26, 255),
                        AssetGlyph::Audio};
            }
            if (entry.assetType.find("prefab") != std::string::npos) {
                return {entry.displayName, IM_COL32(14, 24, 32, 255), IM_COL32(18, 28, 40, 255), IM_COL32(21, 32, 48, 255),
                        AssetGlyph::Prefab};
            }
            if (entry.previewFallback == Assets::AssetPreviewFallback::Mesh) {
                return {entry.displayName, IM_COL32(19, 32, 26, 255), IM_COL32(22, 37, 29, 255), IM_COL32(25, 42, 32, 255),
                        AssetGlyph::Mesh};
            }
            return {entry.displayName, IM_COL32(24, 32, 42, 255), IM_COL32(27, 36, 48, 255), IM_COL32(29, 40, 54, 255),
                    AssetGlyph::Generic};
        }

        void DrawGlyph(ImDrawList *drawList, const AssetGlyph glyph, const ImVec2 center) {
            const ImU32 color = Theme::U32(Theme::Text());
            if (glyph == AssetGlyph::Folder) {
                constexpr float width = 17.0F;
                constexpr float height = 12.0F;
                const ImVec2 folderMin{center.x - width * 0.5F, center.y - height * 0.35F};
                const ImVec2 folderMax{folderMin.x + width, folderMin.y + height};
                drawList->AddRect(folderMin, folderMax, color, 2.0F, ImDrawFlags_RoundCornersAll, 1.5F);
                drawList->AddLine({folderMin.x + 1.5F, folderMin.y}, {folderMin.x + 6.0F, folderMin.y - 3.5F}, color, 1.5F);
                drawList->AddLine({folderMin.x + 6.0F, folderMin.y - 3.5F}, {folderMin.x + 10.0F, folderMin.y - 3.5F}, color, 1.5F);
                drawList->AddLine({folderMin.x + 10.0F, folderMin.y - 3.5F}, {folderMin.x + 12.0F, folderMin.y}, color, 1.5F);
            } else if (glyph == AssetGlyph::Mesh) {
                const std::array front{
                    ImVec2{center.x - 6.0F, center.y - 4.0F},
                    ImVec2{center.x + 2.0F, center.y - 4.0F},
                    ImVec2{center.x + 2.0F, center.y + 4.0F},
                    ImVec2{center.x - 6.0F, center.y + 4.0F},
                };
                const std::array back{
                    ImVec2{center.x - 2.0F, center.y - 7.0F},
                    ImVec2{center.x + 6.0F, center.y - 7.0F},
                    ImVec2{center.x + 6.0F, center.y + 1.0F},
                    ImVec2{center.x - 2.0F, center.y + 1.0F},
                };
                drawList->AddPolyline(front.data(), front.size(), color, ImDrawFlags_Closed, 1.25F);
                drawList->AddPolyline(back.data(), back.size(), color, ImDrawFlags_Closed, 1.25F);
                for (std::size_t index = 0; index < front.size(); ++index)
                    drawList->AddLine(front[index], back[index], color, 1.25F);
            } else if (glyph == AssetGlyph::Image) {
                const ImVec2 minimum{center.x - 8.0F, center.y - 7.0F};
                const ImVec2 maximum{center.x + 8.0F, center.y + 7.0F};
                drawList->AddRect(minimum, maximum, color, 2.0F, ImDrawFlags_RoundCornersAll, 1.3F);
                drawList->AddCircleFilled({center.x + 3.5F, center.y - 2.5F}, 1.7F, color, 10);
                const std::array points{
                    ImVec2{minimum.x + 2.0F, maximum.y - 2.0F}, ImVec2{center.x - 2.0F, center.y},
                    ImVec2{center.x + 1.0F, center.y + 3.0F},   ImVec2{center.x + 5.0F, center.y - 0.5F},
                    ImVec2{maximum.x - 2.0F, maximum.y - 2.0F},
                };
                drawList->AddPolyline(points.data(), points.size(), color, 0, 1.2F);
            } else if (glyph == AssetGlyph::Audio) {
                constexpr std::array heights{4.0F, 8.0F, 12.0F, 7.0F, 10.0F, 5.0F};
                for (std::size_t index = 0; index < heights.size(); ++index) {
                    const float x = center.x - 7.5F + static_cast<float>(index) * 3.0F;
                    drawList->AddLine({x, center.y - heights[index] * 0.5F}, {x, center.y + heights[index] * 0.5F}, color, 1.5F);
                }
            } else if (glyph == AssetGlyph::Generic) {
                const ImVec2 minimum{center.x - 6.0F, center.y - 8.0F};
                const ImVec2 maximum{center.x + 6.0F, center.y + 8.0F};
                drawList->AddRect(minimum, maximum, color, 1.5F, ImDrawFlags_RoundCornersAll, 1.3F);
                drawList->AddLine({center.x + 1.0F, minimum.y}, {maximum.x, center.y - 3.0F}, color, 1.2F);
                drawList->AddLine({maximum.x, center.y - 3.0F}, {center.x + 1.0F, center.y - 3.0F}, color, 1.2F);
            } else if (glyph == AssetGlyph::Prefab) {
                constexpr float halfWidth = 5.2F;
                constexpr float radius = 6.0F;
                const std::array points{
                    ImVec2{center.x, center.y - radius},
                    ImVec2{center.x + halfWidth, center.y - radius * 0.5F},
                    ImVec2{center.x + halfWidth, center.y + radius * 0.5F},
                    ImVec2{center.x, center.y + radius},
                    ImVec2{center.x - halfWidth, center.y + radius * 0.5F},
                    ImVec2{center.x - halfWidth, center.y - radius * 0.5F},
                };
                drawList->AddPolyline(points.data(), points.size(), color, ImDrawFlags_Closed, 1.5F);
            }
        }

        void DrawMeshPreview(ImDrawList *drawList, const ContentBrowserEntry &entry, const ImVec2 previewMin, const ImVec2 previewMax) {
            if (entry.meshPreviewPoints.empty())
                return;

            float minX = std::numeric_limits<float>::max();
            float minY = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float maxY = std::numeric_limits<float>::lowest();
            std::vector<ImVec2> projected;
            projected.reserve(entry.meshPreviewPoints.size());
            for (const ContentBrowserMeshPreviewPoint &point : entry.meshPreviewPoints) {
                const ImVec2 value{point.x * 0.82F + point.z * 0.58F, -point.y + point.x * 0.24F + point.z * 0.18F};
                minX = std::min(minX, value.x);
                minY = std::min(minY, value.y);
                maxX = std::max(maxX, value.x);
                maxY = std::max(maxY, value.y);
                projected.push_back(value);
            }

            const float width = std::max(maxX - minX, 0.0001F);
            const float height = std::max(maxY - minY, 0.0001F);
            const float scale = std::min(std::max(1.0F, previewMax.x - previewMin.x - 16.0F) / width,
                                         std::max(1.0F, previewMax.y - previewMin.y - 16.0F) / height);
            const ImVec2 center{(previewMin.x + previewMax.x) * 0.5F, (previewMin.y + previewMax.y) * 0.5F};
            drawList->PushClipRect(previewMin, previewMax, true);
            for (const ImVec2 point : projected) {
                const ImVec2 screen{center.x + (point.x - (minX + maxX) * 0.5F) * scale,
                                    center.y + (point.y - (minY + maxY) * 0.5F) * scale};
                drawList->AddCircleFilled({screen.x + 1.0F, screen.y + 1.0F}, 1.35F, IM_COL32(0, 0, 0, 70), 6);
                drawList->AddCircleFilled(screen, 1.05F, Theme::U32(Theme::Text()), 6);
            }
            drawList->PopClipRect();
        }

        [[nodiscard]] std::uint64_t PreviewFingerprint(const Assets::AssetPreviewImage &image) {
            std::uint64_t hash = 1469598103934665603ULL;
            const auto mix = [&hash](const std::uint8_t value) {
                hash ^= value;
                hash *= 1099511628211ULL;
            };
            for (unsigned shift = 0; shift < 32; shift += 8) {
                mix(static_cast<std::uint8_t>(image.width >> shift & 0xffU));
                mix(static_cast<std::uint8_t>(image.height >> shift & 0xffU));
            }
            for (const std::uint8_t value : image.pixels)
                mix(value);
            return hash;
        }
    }  // namespace

    /** @copydoc AssetBrowserCardRenderer::Attach */
    void AssetBrowserCardRenderer::Attach(IEditorGuiRenderer *renderer) noexcept {
        m_renderer = renderer;
    }

    /** @copydoc AssetBrowserCardRenderer::Detach */
    void AssetBrowserCardRenderer::Detach() noexcept {
        if (m_renderer != nullptr) {
            for (const auto &[path, preview] : m_previewTextures) {
                static_cast<void>(path);
                m_renderer->DestroyTexture(preview.second);
            }
        }
        m_previewTextures.clear();
        m_renderer = nullptr;
    }

    /** @copydoc AssetBrowserCardRenderer::RetainVisible */
    void AssetBrowserCardRenderer::RetainVisible(const ContentBrowserDirectory &directory, const std::vector<std::size_t> &visibleEntries) {
        if (m_renderer == nullptr || m_previewTextures.empty())
            return;
        std::unordered_set<std::string_view> visiblePaths;
        visiblePaths.reserve(visibleEntries.size());
        for (const std::size_t entryIndex : visibleEntries)
            visiblePaths.emplace(directory.entries[entryIndex].absolutePath);
        for (auto cached = m_previewTextures.begin(); cached != m_previewTextures.end();) {
            if (visiblePaths.contains(cached->first)) {
                ++cached;
                continue;
            }
            m_renderer->DestroyTexture(cached->second.second);
            cached = m_previewTextures.erase(cached);
        }
    }

    std::uintptr_t AssetBrowserCardRenderer::ResolvePreview(const ContentBrowserEntry &entry) {
        if (m_renderer == nullptr || !entry.previewImage.IsValid())
            return 0;
        const std::uint64_t fingerprint = PreviewFingerprint(entry.previewImage);
        if (const auto found = m_previewTextures.find(entry.absolutePath); found != m_previewTextures.end()) {
            if (found->second.first == fingerprint)
                return found->second.second;
            m_renderer->DestroyTexture(found->second.second);
            m_previewTextures.erase(found);
        }
        auto uploaded = m_renderer->CreateTexture(EditorRgba8ImageView{
            .width = entry.previewImage.width,
            .height = entry.previewImage.height,
            .pixels = entry.previewImage.pixels,
        });
        if (uploaded.HasError())
            return 0;
        const std::uintptr_t textureId = std::move(uploaded).Value();
        m_previewTextures.try_emplace(entry.absolutePath, fingerprint, textureId);
        return textureId;
    }

    /** @copydoc AssetBrowserCardRenderer::Draw */
    void AssetBrowserCardRenderer::Draw(const AssetBrowserCardDrawContext &drawContext, const ContentBrowserEntry &entry) {
        const auto &[drawList, font, fontSize, cardMin, cardWidth, hovered, selected, dimmed] = drawContext;
        const AssetCardPresentation asset = PresentEntry(entry);
        const ImVec2 cardMax{cardMin.x + cardWidth, cardMin.y + cardWidth + CardFooterHeight};
        const ImVec2 thumbMax{cardMax.x, cardMin.y + cardWidth};
        drawList->AddRectFilled(cardMin, cardMax, Theme::U32(Theme::Bg3()), CardRadius);
        drawList->AddRectFilled(cardMin, thumbMax, asset.gradientStart, CardRadius, ImDrawFlags_RoundCornersTop);
        drawList->AddRectFilledMultiColor({cardMin.x + 1.0F, cardMin.y + 1.0F}, {thumbMax.x - 1.0F, thumbMax.y}, asset.gradientStart,
                                          asset.gradientMid, asset.gradientMid, asset.gradientEnd);
        if (const std::uintptr_t previewTexture = ResolvePreview(entry); previewTexture != 0)
            drawList->AddImage(previewTexture, {cardMin.x + 3.0F, cardMin.y + 3.0F}, {thumbMax.x - 3.0F, thumbMax.y - 3.0F});
        else if (entry.assetType == "core.mesh" && !entry.meshPreviewPoints.empty())
            DrawMeshPreview(drawList, entry, cardMin, thumbMax);
        else
            DrawGlyph(drawList, asset.glyph, {cardMin.x + cardWidth * 0.5F, cardMin.y + cardWidth * 0.5F});
        if (hovered)
            drawList->AddRectFilled(cardMin, thumbMax, IM_COL32(255, 255, 255, 10), CardRadius, ImDrawFlags_RoundCornersTop);
        if (dimmed) {
            drawList->AddRectFilled(cardMin, cardMax, IM_COL32(6, 10, 14, 118), CardRadius);
            drawList->AddRect({cardMin.x + 2.0F, cardMin.y + 2.0F}, {cardMax.x - 2.0F, cardMax.y - 2.0F}, Theme::U32(Theme::Accent()),
                              CardRadius, ImDrawFlags_RoundCornersAll, 1.0F);
        }
        drawList->AddLine({cardMin.x, thumbMax.y}, thumbMax, Theme::U32(Theme::Border()), 1.0F);
        const ImVec4 outlineColor = selected ? Theme::Accent() : (hovered ? Theme::BorderStrong() : Theme::Border());
        drawList->AddRect(cardMin, cardMax, Theme::U32(outlineColor), CardRadius, ImDrawFlags_RoundCornersAll, selected ? 1.5F : 1.0F);
        const std::string name{asset.name};
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, cardWidth - 8.0F, 0.0F, name.c_str());
        const ImVec2 textPos{cardMin.x + (cardWidth - textSize.x) * 0.5F, thumbMax.y + (CardFooterHeight - fontSize) * 0.5F - 1.0F};
        const ImVec4 clipRect{cardMin.x + 4.0F, thumbMax.y, cardMax.x - 4.0F, cardMax.y};
        drawList->AddText(font, fontSize, textPos, Theme::U32(Theme::Muted()), name.c_str(), nullptr, 0.0F, &clipRect);
    }
}  // namespace Horo::Editor
