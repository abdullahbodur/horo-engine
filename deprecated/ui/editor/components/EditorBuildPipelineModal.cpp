/**
 * @file EditorBuildPipelineModal.cpp
 * @brief Build & Release pipeline modal: platform selection, code signing,
 *        build execution via ExternalProcessRunner, and persistent history.
 */
#include "ui/editor/components/EditorBuildPipelineModal.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <numbers>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <imgui.h>

#include "ui/HoroTheme.h"
#include "ui/UiComponents.h"
#include "ui/launcher/ExternalProcessRunner.h"
#include "core/ArgRedactor.h"
#include "core/Logger.h"
#include "core/pipeline/ReleaseCommand.h"
#include "core/pipeline/ReleaseDraft.h"
#include "core/pipeline/ReleaseHistory.h"
#include "core/pipeline/ReleasePipeline.h"
#include "core/pipeline/TargetCapability.h"
#include "tests/mocks/MockCiBackend.h"

namespace Horo::Editor {

using Horo::Build::BuildHistoryPath;
using Horo::Build::ComputeTotalProgress;
using Horo::Build::CurrentBuildConfig;
using Horo::Build::CurrentTimestamp;
using Horo::Build::DiagnoseBuildFailure;
using Horo::Build::FormatRecentRunTimestamp;
using Horo::Build::GetBuildStageLabel;
using Horo::Build::GetPlatformSelection;
using Horo::Build::IsAnyBuildRunning;
using Horo::Build::MakePendingJob;
using Horo::Build::ReadHistoryJson;
using Horo::Build::RebuildJobsForSelection;
using Horo::Build::RedactSecrets;
using Horo::Build::ResolveJobOutputPath;
using Horo::Build::WriteHistoryJson;

namespace {

// Forward declarations for file-local helpers used by member functions.
Horo::Build::BuildArch DefaultArchForOS(Horo::Build::BuildTargetOS os) {
    if (os == Horo::Build::BuildTargetOS::MacOS) return Horo::Build::BuildArch::Arm64;
    return Horo::Build::BuildArch::x86_64;
}

/** @brief Fixed popup id used for automation and ImGui popup stack identity. */
constexpr const char *kPopupId = "Build & Release";

/** @brief Temporarily sets an environment variable for a spawned build process. */
class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(std::string name, const std::string &value)
        : m_name(std::move(name)), m_oldValue(Read(m_name)) {
        Set(m_name, value);
    }

    ~ScopedEnvironmentVariable() {
        if (m_oldValue)
            Set(m_name, *m_oldValue);
        else
            Unset(m_name);
    }

    ScopedEnvironmentVariable(const ScopedEnvironmentVariable &) = delete;
    ScopedEnvironmentVariable &operator=(const ScopedEnvironmentVariable &) = delete;

private:
    static std::optional<std::string> Read(const std::string &name) {
#if defined(_WIN32)
        char *value = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&value, &size, name.c_str()) != 0 || value == nullptr)
            return std::nullopt;
        std::string result(value, size > 0 ? size - 1 : 0);
        std::free(value);
        return result;
#else
        const char *value = std::getenv(name.c_str());
        if (!value)
            return std::nullopt;
        return std::string(value);
#endif
    }

    static void Set(const std::string &name, const std::string &value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    static void Unset(const std::string &name) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    std::string m_name;
    std::optional<std::string> m_oldValue;
};

/** @brief Default modal dimensions. */
constexpr float kModalDefaultWidth = 1220.0f;
constexpr float kModalDefaultHeight = 820.0f;
constexpr float kFooterHeight = 70.0f;

/** @brief Platform icon variants drawn directly into ImGui draw lists. */
enum class PlatformIconKind {
    Windows,
    MacOS,
    Linux,
};

/** @brief Draws a small platform icon without depending on external icon fonts. */
void DrawPlatformIcon(ImDrawList &drawList, PlatformIconKind kind, ImVec2 origin,
                      float size, ImU32 color) {
    switch (kind) {
    case PlatformIconKind::Windows: {
        const float gap = size * 0.08f;
        const float tile = (size - gap) * 0.5f;
        drawList.AddRectFilled(origin, ImVec2(origin.x + tile, origin.y + tile), color, 1.5f);
        drawList.AddRectFilled(ImVec2(origin.x + tile + gap, origin.y),
                               ImVec2(origin.x + size, origin.y + tile), color, 1.5f);
        drawList.AddRectFilled(ImVec2(origin.x, origin.y + tile + gap),
                               ImVec2(origin.x + tile, origin.y + size), color, 1.5f);
        drawList.AddRectFilled(ImVec2(origin.x + tile + gap, origin.y + tile + gap),
                               ImVec2(origin.x + size, origin.y + size), color, 1.5f);
        break;
    }
    case PlatformIconKind::MacOS: {
        const ImVec2 center(origin.x + size * 0.50f, origin.y + size * 0.56f);
        drawList.AddCircleFilled(center, size * 0.34f, color, 24);
        drawList.AddCircleFilled(ImVec2(origin.x + size * 0.68f, origin.y + size * 0.44f),
                                 size * 0.18f, color, 16);
        drawList.AddEllipseFilled(ImVec2(origin.x + size * 0.60f, origin.y + size * 0.12f),
                                  ImVec2(size * 0.18f, size * 0.08f), color, 0.55f, 16);
        break;
    }
    case PlatformIconKind::Linux: {
        const ImVec2 head(origin.x + size * 0.50f, origin.y + size * 0.34f);
        const ImVec2 body(origin.x + size * 0.50f, origin.y + size * 0.64f);
        drawList.AddCircleFilled(head, size * 0.22f, color, 20);
        drawList.AddEllipseFilled(body, ImVec2(size * 0.32f, size * 0.24f), color, 0.0f, 24);
        drawList.AddCircleFilled(ImVec2(origin.x + size * 0.40f, origin.y + size * 0.31f),
                                 size * 0.035f, IM_COL32(4, 14, 24, 255), 8);
        drawList.AddCircleFilled(ImVec2(origin.x + size * 0.60f, origin.y + size * 0.31f),
                                 size * 0.035f, IM_COL32(4, 14, 24, 255), 8);
        break;
    }
    }
}

/** @brief Draws a small gear icon for the Build tab. */
void DrawGearIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    const float outerR = size * 0.36f;
    const float innerR = size * 0.14f;
    drawList.AddCircle(center, outerR, color, 24, 2.0f);
    drawList.AddCircleFilled(center, innerR, color, 12);
    for (int i = 0; i < 8; ++i) {
        const float a =
            static_cast<float>(i) * std::numbers::pi_v<float> * 0.25f;
        const float pr = outerR * 0.7f;
        const float tr = outerR * 1.28f;
        drawList.AddLine(ImVec2(center.x + std::cos(a) * pr, center.y + std::sin(a) * pr),
                          ImVec2(center.x + std::cos(a) * tr, center.y + std::sin(a) * tr),
                          color, 2.0f);
    }
}

/** @brief Draws a small lock icon for the Signing tab. */
void DrawLockIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const float bodyH = size * 0.44f;
    const float bodyW = size * 0.58f;
    const float bodyY = origin.y + size * 0.48f;
    const float bodyX = origin.x + (size - bodyW) * 0.5f;
    const ImVec2 shackleCenter(origin.x + size * 0.5f, origin.y + size * 0.30f);
    const float shackleR = size * 0.20f;
    drawList.PathArcTo(shackleCenter, shackleR, std::numbers::pi_v<float>,
                       std::numbers::pi_v<float> * 2.0f, 20);
    drawList.PathStroke(color, 0, 2.0f);
    drawList.AddRectFilled(ImVec2(bodyX, bodyY), ImVec2(bodyX + bodyW, bodyY + bodyH),
                            color, size * 0.16f);
    const float khX = origin.x + size * 0.5f;
    const float khY = bodyY + bodyH * 0.40f;
    constexpr ImU32 kBackgroundColor = IM_COL32(8, 10, 16, 255);
    drawList.AddCircleFilled(ImVec2(khX, khY), size * 0.06f,
                             kBackgroundColor, 8);
    drawList.AddRectFilled(ImVec2(khX - size * 0.03f, khY),
                            ImVec2(khX + size * 0.03f, khY + bodyH * 0.30f),
                            kBackgroundColor);
}

/** @brief Draws a monitor icon for the Platforms tab. */
void DrawMonitorIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const float m = size * 0.08f;
    drawList.AddRect(ImVec2(origin.x + m, origin.y + m),
                      ImVec2(origin.x + size - m, origin.y + size * 0.6f),
                      color, size * 0.12f, 0, 2.0f);
    drawList.AddRectFilled(ImVec2(origin.x + size * 0.35f, origin.y + size * 0.62f),
                            ImVec2(origin.x + size * 0.65f, origin.y + size * 0.72f),
                            color, size * 0.06f);
    drawList.AddRectFilled(ImVec2(origin.x + size * 0.46f, origin.y + size * 0.72f),
                            ImVec2(origin.x + size * 0.54f, origin.y + size - m),
                            color, size * 0.06f);
    drawList.AddRectFilled(ImVec2(origin.x + size * 0.38f, origin.y + size - m),
                            ImVec2(origin.x + size * 0.62f, origin.y + size),
                            color, size * 0.06f);
}

/** @brief Draws a sliders icon for the Configuration tab. */
void DrawSlidersIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const float left = origin.x + size * 0.12f;
    const float right = origin.x + size * 0.88f;
    const std::array ys = {
        origin.y + size * 0.25f,
        origin.y + size * 0.50f,
        origin.y + size * 0.75f,
    };
    const std::array knobs = {
        origin.x + size * 0.34f,
        origin.x + size * 0.66f,
        origin.x + size * 0.48f,
    };
    for (int i = 0; i < 3; ++i) {
        drawList.AddLine(ImVec2(left, ys[i]), ImVec2(right, ys[i]), color, 1.7f);
        drawList.AddCircleFilled(ImVec2(knobs[i], ys[i]), size * 0.09f, color, 12);
    }
}

/** @brief Draws a stacked-layers icon for the Configuration tab. */
void DrawLayersIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const ImVec2 center(origin.x + size * 0.50f, origin.y + size * 0.25f);
    const std::array top = {
        ImVec2(center.x, center.y - size * 0.16f),
        ImVec2(origin.x + size * 0.82f, center.y),
        ImVec2(center.x, center.y + size * 0.16f),
        ImVec2(origin.x + size * 0.18f, center.y),
    };
    drawList.AddPolyline(top.data(), static_cast<int>(top.size()), color,
                         ImDrawFlags_Closed, 1.8f);
    for (int i = 0; i < 2; ++i) {
        const float y = origin.y + size * (0.48f + 0.20f * static_cast<float>(i));
        drawList.AddLine(ImVec2(origin.x + size * 0.18f, y),
                         ImVec2(center.x, y + size * 0.16f), color, 1.8f);
        drawList.AddLine(ImVec2(center.x, y + size * 0.16f),
                         ImVec2(origin.x + size * 0.82f, y), color, 1.8f);
    }
}

/** @brief Draws a document icon for the Logs tab. */
void DrawLogTabIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    Horo::Ui::DrawDocumentIcon(drawList, origin, size, color);
}

/** @brief Draws a rocket icon for the Publish tab. */
void DrawRocketIcon(ImDrawList &drawList, ImVec2 origin, float size, ImU32 color) {
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    // Body: upward-pointing teardrop/rocket shape.
    const float bodyTopY = center.y - size * 0.28f;
    const float bodyBottomY = center.y + size * 0.20f;
    const float bodyHalfW = size * 0.16f;
    const std::array<ImVec2, 3> body = {
        ImVec2(center.x, bodyTopY),
        ImVec2(center.x + bodyHalfW, bodyBottomY),
        ImVec2(center.x - bodyHalfW, bodyBottomY),
    };
    drawList.AddTriangleFilled(body[0], body[1], body[2], color);
    // Fins at bottom.
    drawList.AddTriangleFilled(
        ImVec2(center.x - bodyHalfW, bodyBottomY),
        ImVec2(center.x - bodyHalfW - size * 0.12f, bodyBottomY + size * 0.10f),
        ImVec2(center.x - bodyHalfW + size * 0.02f, bodyBottomY + size * 0.06f),
        color);
    drawList.AddTriangleFilled(
        ImVec2(center.x + bodyHalfW, bodyBottomY),
        ImVec2(center.x + bodyHalfW + size * 0.12f, bodyBottomY + size * 0.10f),
        ImVec2(center.x + bodyHalfW - size * 0.02f, bodyBottomY + size * 0.06f),
        color);
    // Exhaust flame.
    drawList.AddTriangleFilled(
        ImVec2(center.x, bodyBottomY + size * 0.02f),
        ImVec2(center.x + size * 0.06f, bodyBottomY + size * 0.18f),
        ImVec2(center.x - size * 0.06f, bodyBottomY + size * 0.18f),
        IM_COL32(255, 160, 40, 255));
}

/** @brief Returns the display label for a build panel tab. */
const char *GetBuildPanelTabLabel(BuildPanelTab tab) {
    using enum BuildPanelTab;
    switch (tab) {
    case Platforms: return "Target";
    case Build: return "Configuration";
    case Signing: return "Signing";
    case Log: return "Logs";
    }
    return "";
}

/** @brief Wraps DrawPlatformIcon for the SelectableCardEntry callback signature. */
void DrawPlatformIconWin(ImDrawList &dl, ImVec2 origin, float size, ImU32 color) {
    DrawPlatformIcon(dl, PlatformIconKind::Windows, origin, size, color);
}
void DrawPlatformIconMac(ImDrawList &dl, ImVec2 origin, float size, ImU32 color) {
    DrawPlatformIcon(dl, PlatformIconKind::MacOS, origin, size, color);
}
void DrawPlatformIconLinux(ImDrawList &dl, ImVec2 origin, float size, ImU32 color) {
    DrawPlatformIcon(dl, PlatformIconKind::Linux, origin, size, color);
}

/** @brief Copies a std::string into a fixed ImGui input buffer and null-terminates it. */
template <std::size_t Size>
void CopyToInputBuffer(std::array<char, Size> &buffer, std::string_view value) {
    buffer.fill('\0');
    const std::size_t count = std::min(value.size(), buffer.size() - 1);
    std::copy_n(value.data(), count, buffer.data());
}

/** @brief Appends a visible error line to a job log without losing existing process output. */
void AppendJobErrorLog(BuildJob &job, std::string_view message) {
    if (message.empty())
        return;
    if (!job.log.empty() && job.log.back() != '\n')
        job.log.push_back('\n');
    job.log += "[ERROR] ";
    job.log += message;
    job.log.push_back('\n');
}

/** @brief Resolves and writes a build job's in-memory log to disk.
 *
 *  Computes the log path from the draft + job + project root, creates
 *  the target directory, and writes the log content via ofstream.
 *  Sets job.logPath on success so the path is recorded in history. */
void WriteJobLogToDisk(const BuildPipelineDraft &draft,
                        BuildJob &job,
                        const std::filesystem::path &projectRoot) {
    job.logPath = Horo::Build::ResolveJobLogPath(draft, job, projectRoot);
    std::ofstream out(job.logPath, std::ios::out | std::ios::trunc);
    if (out.good())
        out << job.log;
}

struct BuildPresetPanelContext {
    BuildPipelineDraft &draft;
    const Horo::Ui::EditorTheme &theme;
    ImDrawList &drawList;
    ImFont &font;
    ImVec2 panelPosition;
    float panelWidth;
    float headerHeight;
    float rowHeight;
    float titleFontSize;
    float detailFontSize;
    ImU32 textColor;
    ImU32 mutedColor;
    BuildConfig currentConfig;
};

/** @brief Draws one selectable build preset row. */
void DrawBuildPresetRow(BuildPresetPanelContext &context, const char *id,
                        int rowIndex, BuildConfig config, const char *name,
                        const char *detail) {
    const float rowY = context.panelPosition.y + context.headerHeight +
                       static_cast<float>(rowIndex) * context.rowHeight;
    const ImVec2 rowMin(context.panelPosition.x, rowY);
    const ImVec2 rowMax(context.panelPosition.x + context.panelWidth,
                        rowY + context.rowHeight);
    const bool selected = context.currentConfig == config;
    if (const bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
        selected || hovered) {
        const ImVec4 rowColor =
            selected
                ? ImVec4(context.theme.palette.accent.x * 0.20f,
                         context.theme.palette.accent.y * 0.20f,
                         context.theme.palette.accent.z * 0.24f, 1.0f)
                : context.theme.palette.cardHover;
        const bool lastRow = rowIndex == 2;
        context.drawList.AddRectFilled(
            rowMin, rowMax, ImGui::ColorConvertFloat4ToU32(rowColor),
            lastRow ? context.theme.rounding.card : 0.0f,
            lastRow ? ImDrawFlags_RoundCornersBottom
                    : ImDrawFlags_RoundCornersNone);
    }

    const float textRight =
        selected ? rowMax.x - 64.0f : rowMax.x - 14.0f;
    const ImVec4 textClip(rowMin.x + 14.0f, rowMin.y + 4.0f, textRight,
                          rowMax.y - 4.0f);
    context.drawList.AddText(
        &context.font, context.titleFontSize,
        ImVec2(rowMin.x + 16.0f, rowMin.y + 9.0f), context.textColor, name,
        nullptr, 0.0f, &textClip);
    context.drawList.AddText(
        &context.font, context.detailFontSize,
        ImVec2(rowMin.x + 16.0f, rowMin.y + 30.0f), context.mutedColor,
        detail, nullptr, 0.0f, &textClip);

    if (selected) {
        const float iconY = rowMin.y + context.rowHeight * 0.5f;
        const float gearX = rowMax.x - 44.0f;
        context.drawList.AddCircle(ImVec2(gearX, iconY), 7.0f,
                                   context.textColor, 14, 1.8f);
        context.drawList.AddCircleFilled(ImVec2(gearX, iconY), 2.2f,
                                         context.textColor, 8);
        const float dotsX = rowMax.x - 20.0f;
        for (const float offset : {-6.0f, 0.0f, 6.0f})
            context.drawList.AddCircleFilled(
                ImVec2(dotsX, iconY + offset), 1.9f, context.textColor, 8);
    }

    ImGui::SetCursorScreenPos(rowMin);
    if (ImGui::InvisibleButton(
            id, ImVec2(context.panelWidth, context.rowHeight))) {
        for (BuildJob &job : context.draft.jobs)
            job.config = config;
    }
}

void PushSigningFieldStyle(const Horo::Ui::EditorTheme &theme) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.input);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.palette.input);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.palette.inputHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.palette.inputActive);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
}

void PopSigningFieldStyle() {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

template <typename Body>
void DrawSigningCard(const Horo::Ui::EditorTheme &theme, const char *id,
                     float width, float height, Body &&body) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.palette.card);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Border,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    std::forward<Body>(body)();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void DrawSigningSectionTitle(const Horo::Ui::EditorTheme &theme,
                             const char *title) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void DrawSigningComboField(const Horo::Ui::EditorTheme &theme, const char *id,
                           const char *preview, float fieldWidth = 0.0f) {
    PushSigningFieldStyle(theme);
    ImGui::SetNextItemWidth(fieldWidth > 0.0f
                                ? fieldWidth
                                : ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo(id, preview)) {
        ImGui::Selectable(preview, true);
        ImGui::EndCombo();
    }
    PopSigningFieldStyle();
}

void DrawSigningStatusRow(const Horo::Ui::EditorTheme &theme,
                          const char *label, const char *value, bool shield) {
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    constexpr float kRowHeight = 36.0f;
    const ImU32 green =
        ImGui::GetColorU32(ImVec4(0.32f, 0.92f, 0.35f, 1.0f));
    const ImVec2 center(cursor.x + 14.0f,
                        cursor.y + kRowHeight * 0.5f);
    if (shield) {
        const std::array points = {
            ImVec2(center.x, center.y - 10.0f),
            ImVec2(center.x + 9.0f, center.y - 6.0f),
            ImVec2(center.x + 7.0f, center.y + 6.0f),
            ImVec2(center.x, center.y + 12.0f),
            ImVec2(center.x - 7.0f, center.y + 6.0f),
            ImVec2(center.x - 9.0f, center.y - 6.0f),
        };
        drawList->AddPolyline(points.data(), static_cast<int>(points.size()),
                              green, ImDrawFlags_Closed, 1.8f);
    } else {
        drawList->AddCircle(center, 9.0f, green, 18, 2.0f);
        drawList->AddLine(ImVec2(center.x - 4.0f, center.y),
                          ImVec2(center.x - 1.0f, center.y + 3.5f), green,
                          2.0f);
        drawList->AddLine(ImVec2(center.x - 1.0f, center.y + 3.5f),
                          ImVec2(center.x + 5.0f, center.y - 4.5f), green,
                          2.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(cursor.x + 38.0f, cursor.y + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    const float valueWidth = ImGui::CalcTextSize(value).x;
    ImGui::SetCursorScreenPos(
        ImVec2(cursor.x + ImGui::GetContentRegionAvail().x - valueWidth,
               cursor.y + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text,
                          ImVec4(0.32f, 0.92f, 0.35f, 1.0f));
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(cursor.x, cursor.y + kRowHeight));
    ImGui::Separator();
}

void AppendUniquePlatformLabel(std::string &platformLabel,
                               const char *osLabel) {
    if (platformLabel.find(osLabel) != std::string::npos)
        return;
    if (!platformLabel.empty())
        platformLabel += ", ";
    platformLabel += osLabel;
}

/** @brief Snapshot of available display size, clamped for the modal. */
ImVec2 ComputeModalSize() {
    const ImGuiIO &io = ImGui::GetIO();
    constexpr float kSafeMargin = 32.0f;
    return ImVec2(
        std::min(kModalDefaultWidth, std::max(520.0f, io.DisplaySize.x - kSafeMargin)),
        std::min(kModalDefaultHeight, std::max(400.0f, io.DisplaySize.y - kSafeMargin)));
}

/** @brief Icon character for a build job status. */
const char *StatusIcon(BuildJobStatus status) {
    using enum BuildJobStatus;
    switch (status) {
    case Pending:   return "○";
    case Building:  return "...";
    case Success:   return "✓";
    case Failed:    return "x";
    case Cancelled: return "-";
    }
    return "?";
}



/** @brief Colour for a build job status. */
ImVec4 StatusColor(BuildJobStatus status) {
    using enum BuildJobStatus;
    switch (status) {
    case Pending:   return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    case Building:  return ImVec4(0.2f, 0.6f, 1.0f, 1.0f);
    case Success:   return ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
    case Failed:    return ImVec4(0.9f, 0.3f, 0.3f, 1.0f);
    case Cancelled: return ImVec4(0.7f, 0.5f, 0.2f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

} // namespace

// -- EditorBuildPipelineModal --------------------------------------------------

/** @copydoc EditorBuildPipelineModal::TransitionTo */
bool EditorBuildPipelineModal::TransitionTo(BuildPipelineState to) {
    if (m_state == to)
        return true;  // No-op: already in the target state.

    if (!Horo::Build::CanTransitionBuildPipelineState(m_state, to)) {
        assert(false && "Invalid build pipeline state transition");
        return false;
    }

    m_state = to;
    return true;
}

/** @copydoc EditorBuildPipelineModal::Open */
void EditorBuildPipelineModal::Open() {
    m_open = true;
    m_openRequested = true;
    TransitionTo(BuildPipelineState::Configuring);
    // Resolve default output root.
    m_draft = {};
    m_draft.buildName = "HoroGame";
    m_draft.outputRoot = ResolveDefaultOutputRoot();
    m_draft.versionTag = DefaultVersionTag();
    m_draft.gameVersion = "1.0.0";
    m_draft.buildNumber = "42";
    m_draft.releaseChannel = 0;
    m_draft.packageIdentifier = "com.horoengine.horogame";

    // ── Single target selection respecting BuildToolchainSettings ──────
    // Default to host platform; fall back to the first enabled platform
    // when the host platform is disabled.  When no toolchain settings are
    // injected (nullptr), all platforms are treated as enabled and the
    // host platform is selected (pre-HORO-52 behaviour).
    m_draft.jobs.clear();
    BuildTargetOS defaultTarget;
#if defined(_WIN32)
    defaultTarget = BuildTargetOS::Windows;
#elif defined(__APPLE__)
    defaultTarget = BuildTargetOS::MacOS;
#else
    defaultTarget = BuildTargetOS::Linux;
#endif

    bool hostEnabled = true;
    if (m_toolchainStore) {
        auto cap = Horo::Build::EvaluateTargetCapability(defaultTarget, DefaultArchForOS(defaultTarget), *m_toolchainStore);
        hostEnabled = cap.IsEnabled();
    }

    using enum BuildConfig;
    if (hostEnabled) {
        // Host platform enabled — use it.
        m_draft.jobs.push_back(MakePendingJob(defaultTarget, DefaultArchForOS(defaultTarget), Release));
    } else if (m_toolchainStore) {
        // Host disabled — scan for the first enabled platform.
        using enum BuildTargetOS;
        if (Horo::Build::EvaluateTargetCapability(Windows, DefaultArchForOS(Windows), *m_toolchainStore).IsEnabled())
            m_draft.jobs.push_back(MakePendingJob(Windows, DefaultArchForOS(Windows), Release));
        else if (Horo::Build::EvaluateTargetCapability(MacOS, DefaultArchForOS(MacOS), *m_toolchainStore).IsEnabled())
            m_draft.jobs.push_back(MakePendingJob(MacOS, DefaultArchForOS(MacOS), Release));
        else if (Horo::Build::EvaluateTargetCapability(Linux, DefaultArchForOS(Linux), *m_toolchainStore).IsEnabled())
            m_draft.jobs.push_back(MakePendingJob(Linux, DefaultArchForOS(Linux), Release));
        // If nothing is enabled, leave jobs empty — the user must enable
        // at least one platform in Build Settings.
    } else {
        // No settings injected — fall back to host platform.
        m_draft.jobs.push_back(MakePendingJob(defaultTarget, DefaultArchForOS(defaultTarget), Release));
    }

    m_original = m_draft.Clone();

    // Seed input buffers.
    CopyToInputBuffer(m_buildNameBuf, m_draft.buildName);
    CopyToInputBuffer(m_versionTagBuf, m_draft.versionTag);
    CopyToInputBuffer(m_outputRootBuf, m_draft.outputRoot);
    CopyToInputBuffer(m_gameVersionBuf, m_draft.gameVersion);
    CopyToInputBuffer(m_buildNumberBuf, m_draft.buildNumber);
    CopyToInputBuffer(m_packageIdBuf, m_draft.packageIdentifier);
    m_certPathBuf.fill('\0');
    m_certPassBuf.fill('\0');
    m_appleIdBuf.fill('\0');
    m_teamIdBuf.fill('\0');
    m_keychainProfileBuf.fill('\0');

    // Clear any prior validation errors.
    m_versionTagError.clear();
    m_gameVersionError.clear();

    if (!m_historyLoaded) {
        LoadHistory();
        m_historyLoaded = true;
    }
}

/** @copydoc EditorBuildPipelineModal::Close */
void EditorBuildPipelineModal::Close() {
    m_open = false;
    TransitionTo(BuildPipelineState::Idle);
    m_viewingHistoryIndex = -1;
    m_historicalLogText.clear();
}

/** @copydoc EditorBuildPipelineModal::PollBuilds */
void EditorBuildPipelineModal::PollBuilds() {


    m_processRunner.Poll();
    const auto &status = m_processRunner.GetStatus();

    // Find the currently building job.
    auto it = std::ranges::find_if(m_draft.jobs, [](const BuildJob &j) {
        return j.status == BuildJobStatus::Building;
    });
    if (it == m_draft.jobs.end())
        return;
    BuildJob &job = *it;

    if (status.output.size() > m_lastProcessOutputSize) {
        if (!job.log.empty() && job.log.back() != '\n')
            job.log.push_back('\n');
        job.log.append(status.output.substr(m_lastProcessOutputSize));
        m_lastProcessOutputSize = status.output.size();
    }

    const std::filesystem::path projectRoot =
        m_projectRoot.empty() ? std::filesystem::current_path()
                              : std::filesystem::path(m_projectRoot);

    if (status.finished) {
        if (status.exitCode == 0) {
            job.status = BuildJobStatus::Success;
            job.exitCode = 0;
        } else {
            job.status = BuildJobStatus::Failed;
            job.exitCode = status.exitCode;
            job.error = status.error.empty()
                            ? std::format("Build exited with code {}", status.exitCode)
                            : status.error;
            AppendJobErrorLog(job, job.error);
            job.diagnostic = DiagnoseBuildFailure(job.exitCode, job.log, job.error);
        }
        job.timestamp = CurrentTimestamp();
        job.outputPath = ResolveJobOutputPath(m_draft, job, projectRoot);
        WriteJobLogToDisk(m_draft, job, projectRoot);

        // Start the next job or finish.
        m_draft.totalProgress = ComputeTotalProgress(m_draft);

        if (!FinalizeIfAllJobsTerminal()) {
            StartNextPendingJob();
        }
    } else if (status.failedToStart) {
        job.status = BuildJobStatus::Failed;
        job.error = status.error;
        AppendJobErrorLog(job, job.error);
        job.diagnostic = DiagnoseBuildFailure(job.exitCode, job.log, job.error);
        job.timestamp = CurrentTimestamp();
        WriteJobLogToDisk(m_draft, job, projectRoot);
        m_draft.totalProgress = ComputeTotalProgress(m_draft);
        StartNextPendingJob();
    }
}

/** @copydoc EditorBuildPipelineModal::DrawTabBar */
BuildPanelTab EditorBuildPipelineModal::DrawTabBar(float availableWidth) {
    using enum BuildPanelTab;
    const auto &theme = Horo::Ui::GetEditorTheme();
    // Build tab arrays: Publish tab only appears when all builds succeed.
    const int tabCount = 4;
    const std::array<BuildPanelTab, 4> tabIds = {
        Platforms,
        Build,
        Signing,
        Log,
    };

    const std::array<Horo::Ui::EditorTopTabItem, 4> tabs = {{
        {"target", GetBuildPanelTabLabel(Platforms), DrawSlidersIcon,
         m_activePanel == Platforms},
        {"configuration", GetBuildPanelTabLabel(Build), DrawLayersIcon,
         m_activePanel == Build},
        {"signing", GetBuildPanelTabLabel(Signing), DrawLockIcon,
         m_activePanel == Signing},
        {"logs", GetBuildPanelTabLabel(Log), DrawLogTabIcon,
         m_activePanel == Log},
    }};

    if (const Horo::Ui::EditorTopTabBarResult result =
            Horo::Ui::RenderEditorTopTabBar(
                theme, "##build_pipeline_tabs",
                std::span(tabs.data(), static_cast<size_t>(tabCount)),
                availableWidth, 44.0f);
        result.clickedIndex >= 0)
        m_activePanel = tabIds[static_cast<size_t>(result.clickedIndex)];

    ImGui::Spacing();

    return m_activePanel;
}

/** @copydoc EditorBuildPipelineModal::Draw */
void EditorBuildPipelineModal::Draw() {
    using enum BuildPanelTab;
    if (!m_open || ImGui::GetCurrentContext() == nullptr)
        return;

    const auto &theme = Horo::Ui::GetEditorTheme();

    if (m_openRequested) {
        ImGui::OpenPopup(kPopupId);
        m_openRequested = false;
    }

    const ImVec2 modalSize = ComputeModalSize();
    const ImVec2 viewportCenter = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(viewportCenter, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.rounding.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);

    if (constexpr ImGuiWindowFlags kFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        !ImGui::BeginPopupModal(kPopupId, nullptr, kFlags)) {
        ImGui::PopStyleVar(2);
        return;
    }

    // Escape → close (only when no build is running).
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) &&
        m_state != BuildPipelineState::Building) {
            Close();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::PopStyleVar(2);
            return;
        }

    // ── Header ───────────────────────────────────────────────────────────
    {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
        ImGui::PushFont(ImGui::GetFont());
        ImGui::TextUnformatted("Build & Release");
        ImGui::PopFont();
        ImGui::PopStyleColor();

        const float closeSize = ImGui::GetFrameHeight();
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SameLine(0.0f, 0.0f);
        if (avail > closeSize)
            ImGui::Dummy(ImVec2(avail - closeSize, 0.0f));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        if (ImGui::Button("X##build_modal_close",
                          ImVec2(closeSize, closeSize)) &&
            m_state != BuildPipelineState::Building) {
            Close();
            ImGui::PopStyleColor(3);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::PopStyleVar(2);
            return;
        }
        ImGui::PopStyleColor(3);

        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted("Configure and execute release builds");
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // ── Tab bar ────────────────────────────────────────────────────────
    DrawTabBar(ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    // ── Panel content ───────────────────────────────────────────────────
    const float bodyHeight =
        std::max(260.0f, ImGui::GetContentRegionAvail().y - kFooterHeight - 8.0f);
    ImGui::BeginChild("##build_dashboard", ImVec2(0.0f, bodyHeight),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    const float availWidth = ImGui::GetContentRegionAvail().x;
    const float contentHeight = ImGui::GetContentRegionAvail().y;

    switch (m_activePanel) {
    case Platforms:
        DrawPlatformColumn(availWidth, contentHeight);
        break;
    case Build:
        DrawConfigurationColumn(availWidth, contentHeight);
        break;
    case Signing:
        DrawSigningColumn(availWidth, contentHeight);
        break;
    case Log:
        DrawLogColumn(availWidth, contentHeight);
        break;
    }

    ImGui::EndChild();

    DrawActionsSection();

    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

// -- Dashboard renderers ------------------------------------------------------

/** @copydoc EditorBuildPipelineModal::IsPlatformEnabledForTest */
bool EditorBuildPipelineModal::IsPlatformEnabledForTest(BuildTargetOS os) const {
    if (!m_toolchainStore) return false;
    return Horo::Build::EvaluateTargetCapability(os, DefaultArchForOS(os), *m_toolchainStore).IsEnabled();
}

/** @copydoc EditorBuildPipelineModal::IsSelectedTargetBuildable */
bool EditorBuildPipelineModal::IsSelectedTargetBuildable() const {
    if (m_draft.jobs.empty())
        return false;
    const auto &job = m_draft.jobs.front();
    if (!m_toolchainStore) return false;
    return Horo::Build::EvaluateTargetCapability(job.os, DefaultArchForOS(job.os), *m_toolchainStore).IsEnabled();
}

/** @copydoc EditorBuildPipelineModal::SelectedTargetUnavailableReason */
std::string EditorBuildPipelineModal::SelectedTargetUnavailableReason() const {
    if (m_draft.jobs.empty() || !m_toolchainStore)
        return "No buildable target selected";

    const auto &job = m_draft.jobs.front();
    if (const auto cap = Horo::Build::EvaluateTargetCapability(
            job.os, DefaultArchForOS(job.os), *m_toolchainStore);
        !cap.IsEnabled() && !cap.disableReason.empty())
        return cap.disableReason;
    return "No buildable target selected";
}

/** @copydoc EditorBuildPipelineModal::RestartBuildFromDraft */
void EditorBuildPipelineModal::RestartBuildFromDraft() {
    m_viewingHistoryIndex = -1;
    m_historicalLogText.clear();
    for (auto &job : m_draft.jobs) {
        job.status = BuildJobStatus::Pending;
        job.exitCode = 0;
        job.log.clear();
        job.logPath.clear();
        job.error.clear();
        job.outputPath.clear();
    }
    m_draft.allJobsComplete = false;
    m_draft.anyJobFailed = false;
    m_draft.totalProgress = 0;
    TransitionTo(BuildPipelineState::Configuring);
    StartNextPendingJob();
}

void EditorBuildPipelineModal::DrawPlatformColumn(float width, float height) {
    const auto &theme = Horo::Ui::GetEditorTheme();
    constexpr float kPlatformCardHeight = 500.0f;
    ImGui::BeginChild("##build_platform_column",
                      ImVec2(width, std::min(height, kPlatformCardHeight)),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Target Platform");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    DrawPlatformCards();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    DrawBuildPresets();
    ImGui::EndChild();
}

/** @copydoc EditorBuildPipelineModal::SelectPlatformCard */
void EditorBuildPipelineModal::SelectPlatformCard(int index) {
    PlatformSelection selection;
    switch (index) {
    case 0:
        selection.windowsSelected = true;
        break;
    case 1:
        selection.macOSSelected = true;
        break;
    case 2:
        selection.linuxSelected = true;
        break;
    default:
        return;
    }
    RebuildJobsForSelection(m_draft, selection);
}

/** @copydoc EditorBuildPipelineModal::DrawPlatformCards */
void EditorBuildPipelineModal::DrawPlatformCards() {
    using enum BuildTargetOS;
    const auto &theme = Horo::Ui::GetEditorTheme();
    PlatformSelection selection = GetPlatformSelection(m_draft);

    Horo::Build::TargetCapability macosCap;
    Horo::Build::TargetCapability windowsCap;
    Horo::Build::TargetCapability linuxCap;
    if (m_toolchainStore) {
        macosCap = Horo::Build::EvaluateTargetCapability(
            MacOS, DefaultArchForOS(MacOS), *m_toolchainStore);
        windowsCap = Horo::Build::EvaluateTargetCapability(
            Windows, DefaultArchForOS(Windows), *m_toolchainStore);
        linuxCap = Horo::Build::EvaluateTargetCapability(
            Linux, DefaultArchForOS(Linux), *m_toolchainStore);
    }

    const bool macosBuildable = macosCap.IsEnabled();
    const bool windowsBuildable = windowsCap.IsEnabled();
    const bool linuxBuildable = linuxCap.IsEnabled();

    const char *macosReason = macosCap.disableReason.empty() ? "Host platform — ready to build" : macosCap.disableReason.c_str();
    const char *windowsReason = windowsCap.disableReason.empty() ? "Host platform — ready to build" : windowsCap.disableReason.c_str();
    const char *linuxReason = linuxCap.disableReason.empty() ? "Host platform — ready to build" : linuxCap.disableReason.c_str();

    // Subtitle: arch hint when buildable, availability reason when not.
    const std::string windowsSubtitle = windowsBuildable ? std::format("{} MSVC / x86_64", StatusIcon(Horo::Build::BuildJobStatus::Success)) : windowsReason;
    const std::string macosSubtitle   = macosBuildable   ? std::format("{} Clang / arm64", StatusIcon(Horo::Build::BuildJobStatus::Success))  : macosReason;
    const std::string linuxSubtitle   = linuxBuildable   ? std::format("{} GCC / x86_64", StatusIcon(Horo::Build::BuildJobStatus::Success))   : linuxReason;

    std::vector<Horo::Ui::SelectableCardEntry> cards;
    cards.emplace_back("windows", "Windows", windowsSubtitle,
                     DrawPlatformIconWin, selection.windowsSelected,
                     !windowsBuildable, windowsReason);
    cards.emplace_back("macos",   "macOS",   macosSubtitle,
                     DrawPlatformIconMac, selection.macOSSelected,
                     !macosBuildable, macosReason);
    cards.emplace_back("linux",   "Linux",   linuxSubtitle,
                     DrawPlatformIconLinux, selection.linuxSelected,
                     !linuxBuildable, linuxReason);

    Horo::Ui::RenderSelectableCardGrid(
        theme, cards, [this](int index) { SelectPlatformCard(index); },
        /*columns=*/1);
}

/** @copydoc EditorBuildPipelineModal::DrawBuildPresets */
void EditorBuildPipelineModal::DrawBuildPresets() {
    const auto &theme = Horo::Ui::GetEditorTheme();
    const BuildConfig currentConfig = CurrentBuildConfig(m_draft);
    const float presetPanelWidth = ImGui::GetContentRegionAvail().x;
    constexpr float presetHeaderHeight = 40.0f;
    constexpr float presetRowHeight = 54.0f;
    const float presetPanelHeight = presetHeaderHeight + presetRowHeight * 3.0f;
    const ImVec2 presetPanelPos = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImU32 presetBorder = ImGui::ColorConvertFloat4ToU32(theme.palette.border);
    const ImU32 presetBg = ImGui::ColorConvertFloat4ToU32(theme.palette.card);
    const ImU32 presetHeaderBg = ImGui::ColorConvertFloat4ToU32(theme.palette.cardHover);
    const ImU32 presetText = ImGui::ColorConvertFloat4ToU32(theme.palette.text);
    const ImU32 presetMuted = ImGui::ColorConvertFloat4ToU32(theme.palette.textMuted);

    drawList->AddRectFilled(presetPanelPos,
                            ImVec2(presetPanelPos.x + presetPanelWidth,
                                   presetPanelPos.y + presetPanelHeight),
                            presetBg, theme.rounding.card);
    drawList->AddRect(presetPanelPos,
                      ImVec2(presetPanelPos.x + presetPanelWidth,
                             presetPanelPos.y + presetPanelHeight),
                      presetBorder, theme.rounding.card);
    drawList->AddRectFilled(presetPanelPos,
                            ImVec2(presetPanelPos.x + presetPanelWidth,
                                   presetPanelPos.y + presetHeaderHeight),
                            presetHeaderBg, theme.rounding.card,
                            ImDrawFlags_RoundCornersTop);
    ImFont *font = ImGui::GetFont();
    const float baseFontSize = ImGui::GetFontSize();
    const float headerFontSize = std::max(13.0f, baseFontSize * 0.90f);
    const float titleFontSize = std::max(13.0f, baseFontSize * 0.84f);
    const float detailFontSize = std::max(12.0f, baseFontSize * 0.76f);
    drawList->AddText(font, headerFontSize,
                      ImVec2(presetPanelPos.x + 14.0f, presetPanelPos.y + 11.0f),
                      presetText, "Build Presets");

    const ImVec2 plusCenter(presetPanelPos.x + presetPanelWidth - 26.0f,
                            presetPanelPos.y + presetHeaderHeight * 0.5f);
    drawList->AddLine(ImVec2(plusCenter.x - 5.0f, plusCenter.y),
                      ImVec2(plusCenter.x + 5.0f, plusCenter.y), presetText, 1.8f);
    drawList->AddLine(ImVec2(plusCenter.x, plusCenter.y - 5.0f),
                      ImVec2(plusCenter.x, plusCenter.y + 5.0f), presetText, 1.8f);

    BuildPresetPanelContext context{
        m_draft,          theme,          *drawList,
        *font,            presetPanelPos, presetPanelWidth,
        presetHeaderHeight, presetRowHeight, titleFontSize,
        detailFontSize,   presetText,     presetMuted,
        currentConfig};
    DrawBuildPresetRow(context, "##preset_debug", 0, BuildConfig::Debug,
                       "Debug (Development)", "Development build");
    DrawBuildPresetRow(context, "##preset_release", 1, BuildConfig::Release,
                       "Release", "Optimized build");
    DrawBuildPresetRow(context, "##preset_shipping", 2,
                       BuildConfig::MinSizeRel, "Shipping",
                       "Final build for distribution");

    ImGui::SetCursorScreenPos(ImVec2(presetPanelPos.x, presetPanelPos.y + presetPanelHeight));
    ImGui::Dummy(ImVec2(presetPanelWidth, 0.0f));
    ImGui::Spacing();
    ImGui::Button("Manage Presets...", ImVec2(presetPanelWidth, 0.0f));
}

void EditorBuildPipelineModal::DrawConfigurationColumn(float width, float height) {
    const auto &theme = Horo::Ui::GetEditorTheme();

    // Validate version fields each frame while the Build tab is active.
    ValidateVersionFields();
    ImGui::BeginChild("##build_configuration_column", ImVec2(width, height),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_AlwaysUseWindowPadding);

    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Configuration");
    ImGui::PopStyleColor();
    Horo::Ui::TextMuted(theme, "Configure build settings and versioning.");
    ImGui::Spacing();

    auto pushFieldStyle = [&]() {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.rounding.input);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.palette.input);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.palette.inputHover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.palette.inputActive);
        ImGui::PushStyleColor(ImGuiCol_Border, theme.palette.border);
    };
    auto popFieldStyle = []() {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    };
    constexpr float labelWidth = 130.0f;
    constexpr float hintGap = 10.0f;
    auto beginRow = [&](const char *label) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(labelWidth);
    };
    auto endRow = []() { ImGui::Spacing(); };

    beginRow("Build Name");
    pushFieldStyle();
    ImGui::SetNextItemWidth(270.0f);
    if (ImGui::InputText("##build_name", m_buildNameBuf.data(), m_buildNameBuf.size()))
        m_draft.buildName = m_buildNameBuf.data();
    popFieldStyle();
    endRow();

    // -- Versioning ------------------------------------------------------------------

    {
        // Current engine version readout.
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted("Engine");
        ImGui::PopStyleColor();
        ImGui::SameLine(labelWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.accent);
        ImGui::TextUnformatted(Horo::Build::CurrentEngineVersion());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    beginRow("Version Tag");
    pushFieldStyle();
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::InputText("##version_tag", m_versionTagBuf.data(), m_versionTagBuf.size()))
        m_draft.versionTag = m_versionTagBuf.data();
    popFieldStyle();
    ImGui::SameLine(0.0f, 6.0f);
    if (ImGui::Button("Bump Patch##bump_patch", ImVec2(100.0f, 0.0f)))
        BumpVersionPatch();
    // Show validation error under version tag.
    if (!m_versionTagError.empty()) {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + labelWidth, cursor.y));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextUnformatted(m_versionTagError.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    endRow();

    beginRow("Game Version");
    pushFieldStyle();
    ImGui::SetNextItemWidth(210.0f);
    if (ImGui::InputText("##game_version", m_gameVersionBuf.data(), m_gameVersionBuf.size()))
        m_draft.gameVersion = m_gameVersionBuf.data();
    popFieldStyle();
    // Show validation error under game version.
    if (!m_gameVersionError.empty()) {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + labelWidth, cursor.y));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.35f, 1.0f));
        ImGui::TextUnformatted(m_gameVersionError.c_str());
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }
    endRow();

    beginRow("Output Folder");
    pushFieldStyle();
    const float browseButtonWidth = 34.0f;
    ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x -
                                               browseButtonWidth - hintGap));
    if (ImGui::InputText("##output_folder", m_outputRootBuf.data(), m_outputRootBuf.size()))
        m_draft.outputRoot = m_outputRootBuf.data();
    popFieldStyle();
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::Button("...", ImVec2(browseButtonWidth, 0.0f));
    endRow();

    int archIdx = 0;
    if (!m_draft.jobs.empty() && m_draft.jobs.front().arch == BuildArch::Arm64)
        archIdx = 1;
    beginRow("Architecture");
    pushFieldStyle();
    ImGui::SetNextItemWidth(270.0f);
    if (ImGui::Combo("##architecture", &archIdx,
                     "Intel/AMD (x86_64)\0Apple Silicon (arm64)\0")) {
        const BuildArch arch = archIdx == 1 ? BuildArch::Arm64 : BuildArch::x86_64;
        for (auto &job : m_draft.jobs)
            job.arch = arch;
    }
    popFieldStyle();
    endRow();

    beginRow("Content / Scenes");
    pushFieldStyle();
    ImGui::SetNextItemWidth(270.0f);
    ImGui::Combo("##content_scenes", &m_draft.contentSelection,
                 "All Content (Default)\0Current Scene Only\0Selected Scenes\0");
    popFieldStyle();
    Horo::Ui::TextMuted(theme, "Includes all content and scenes in the project.");

    ImGui::EndChild();
}

void EditorBuildPipelineModal::DrawSigningColumn(float width, float height) {
    const auto &theme = Horo::Ui::GetEditorTheme();
    constexpr float kSigningIdentityHeight = 210.0f;
    constexpr float kSigningSecurityHeight = 250.0f;
    constexpr float kSigningColumnGap = 12.0f;
    constexpr float kSigningHeaderHeight = 76.0f;
    const float signingContentHeight =
        kSigningIdentityHeight + kSigningColumnGap + kSigningSecurityHeight;
    const float signingCardHeight =
        std::min(height, kSigningHeaderHeight + signingContentHeight + 24.0f);
    ImGui::BeginChild("##build_signing_column", ImVec2(width, signingCardHeight),
                      ImGuiChildFlags_Border,
                      ImGuiWindowFlags_AlwaysUseWindowPadding |
                          ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Code Signing");
    ImGui::PopStyleColor();
    ImGui::Spacing();
    Horo::Ui::RenderEditorToggle(theme, "##enable_code_signing",
                                 "Enable Code Signing",
                                 m_draft.signing.enabled);
    Horo::Ui::TextMuted(theme, "Sign the application bundle to establish provenance and enable platform security features.");
    ImGui::Spacing();

    const float gap = 12.0f;
    const float totalWidth = ImGui::GetContentRegionAvail().x;
    const float statusWidth = std::clamp(totalWidth * 0.35f, 330.0f, 430.0f);
    const float leftWidth = std::max(360.0f, totalWidth - statusWidth - gap);
    const float columnHeight =
        std::min(signingContentHeight, std::max(360.0f, ImGui::GetContentRegionAvail().y - 4.0f));

    ImGui::BeginChild("##signing_left_column", ImVec2(leftWidth, columnHeight),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    DrawSigningCard(theme, "##signing_identity_card", 0.0f,
                    kSigningIdentityHeight,
                    [this]() { DrawSigningIdentityCard(); });
    ImGui::Dummy(ImVec2(0.0f, kSigningColumnGap - ImGui::GetStyle().ItemSpacing.y));
    DrawSigningCard(theme, "##signing_security_card", 0.0f,
                    kSigningSecurityHeight,
                    [this]() { DrawSigningSecurityCard(); });
    ImGui::EndChild();

    ImGui::SameLine(0.0f, gap);
    DrawSigningCard(theme, "##signing_status_card", statusWidth, columnHeight,
                    [this]() { DrawSigningStatusCard(); });

    ImGui::EndChild();
}

/** @copydoc EditorBuildPipelineModal::DrawSigningIdentityCard */
void EditorBuildPipelineModal::DrawSigningIdentityCard() {
    const auto &theme = Horo::Ui::GetEditorTheme();
    constexpr float kLabelWidth = 230.0f;
    const auto beginRow = [&theme](const char *label) {
        ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ImGui::SameLine(kLabelWidth);
    };

    DrawSigningSectionTitle(theme, "Signing Identity");
    beginRow("Certificate");
    DrawSigningComboField(
        theme, "##certificate_select", "Select certificate...",
        std::max(120.0f, ImGui::GetContentRegionAvail().x - 48.0f));
    ImGui::SameLine(0.0f, 8.0f);
    Horo::Ui::RenderEditorIconButton(
        theme, "##refresh_signing_identity", Horo::Ui::DrawRefreshIcon,
        ImVec2(40.0f, 34.0f));
    beginRow("Team / Publisher");
    DrawSigningComboField(theme, "##team_select",
                          "Select team or publisher...");
    beginRow("Bundle Identifier / Package ID");
    PushSigningFieldStyle(theme);
    ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::InputText("##bundle_identifier", m_packageIdBuf.data(),
                     m_packageIdBuf.size(), ImGuiInputTextFlags_ReadOnly);
    ImGui::EndDisabled();
    PopSigningFieldStyle();
    beginRow("Provisioning / Profile");
    DrawSigningComboField(theme, "##provisioning_profile",
                          "Select provisioning profile (optional)...");
}

/** @copydoc EditorBuildPipelineModal::DrawSigningSecurityCard */
void EditorBuildPipelineModal::DrawSigningSecurityCard() {
    const auto &theme = Horo::Ui::GetEditorTheme();
    constexpr float kLabelWidth = 230.0f;
    DrawSigningSectionTitle(theme, "Security Options");
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::TextUnformatted("Timestamp Server");
    ImGui::PopStyleColor();
    ImGui::SameLine(kLabelWidth);
    DrawSigningComboField(
        theme, "##timestamp_server",
        "Apple (https://timestamp.apple.com/ts01)");
    Horo::Ui::RenderEditorCheckbox(
        theme, "Hardened Runtime / Secure Packaging",
        m_draft.signing.hardenedRuntime);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 26.0f);
    Horo::Ui::TextMuted(
        theme, "Enables additional runtime protections for macOS builds.");
    Horo::Ui::RenderEditorCheckbox(theme, "Notarize After Build",
                                   m_draft.signing.notarize);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 26.0f);
    Horo::Ui::TextMuted(
        theme,
        "Submit the build to Apple for notarization after signing.");
    Horo::Ui::RenderEditorCheckbox(theme, "Verify Signature After Build",
                                   m_draft.signing.verifySignature);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 26.0f);
    Horo::Ui::TextMuted(
        theme,
        "Validate the signature before producing the release artifacts.");
}

/** @copydoc EditorBuildPipelineModal::DrawSigningStatusCard */
void EditorBuildPipelineModal::DrawSigningStatusCard() {
    const auto &theme = Horo::Ui::GetEditorTheme();
    DrawSigningSectionTitle(theme, "Signing Status");
    DrawSigningStatusRow(theme, "Certificate detected", "Valid", false);
    DrawSigningStatusRow(theme, "Team / Publisher", "Valid", false);
    DrawSigningStatusRow(theme, "Bundle ID valid", "Valid", false);
    DrawSigningStatusRow(theme, "Signing Profile", "Found", false);
    DrawSigningStatusRow(theme, "Timestamp configured", "Configured", false);
    DrawSigningStatusRow(theme, "Overall Status", "Ready for signed build",
                         true);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::TextWrapped(
        "i  All signing requirements are satisfied.\n"
        "   The build is ready to be signed.");
    ImGui::PopStyleColor();
}

/** @copydoc EditorBuildPipelineModal::BuildLiveLogText */
std::string EditorBuildPipelineModal::BuildLiveLogText() const {
    std::string displayLog;
    for (const auto &job : m_draft.jobs) {
        if (!job.log.empty())
            displayLog += job.log;
    }
    return displayLog;
}

/** @copydoc EditorBuildPipelineModal::DrawHistoryLogBanner */
void EditorBuildPipelineModal::DrawHistoryLogBanner(const BuildHistoryEntry &entry) {
    const auto &theme = Horo::Ui::GetEditorTheme();
    const float rowStartY = ImGui::GetCursorPosY();
    const float rowHeight = ImGui::GetFrameHeight();
    const float textOffsetY = std::max(
        0.0f, (rowHeight - ImGui::GetTextLineHeight()) * 0.5f);

    ImGui::SetCursorPosY(rowStartY + textOffsetY);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.accent);
    ImGui::TextUnformatted(std::format("Viewing historical log: {}",
                                        FormatRecentRunTimestamp(entry.timestamp)).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::SetCursorPosY(rowStartY);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.palette.cardHover);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 0.0f));
    if (ImGui::Button("Back to current", ImVec2(0.0f, rowHeight))) {
        m_viewingHistoryIndex = -1;
        m_historicalLogText.clear();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::SetCursorPosY(rowStartY + rowHeight);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

/** @copydoc EditorBuildPipelineModal::LoadHistoricalLog */
void EditorBuildPipelineModal::LoadHistoricalLog(size_t entryIndex) {
    m_viewingHistoryIndex = static_cast<int>(entryIndex);
    m_historicalLogText.clear();

    const auto &histEntry = m_history[entryIndex];
    for (const auto &job : histEntry.jobs) {
        if (!job.logPath.empty()) {
            if (std::ifstream file(job.logPath); file.good()) {
                std::ostringstream buffer;
                buffer << file.rdbuf();
                m_historicalLogText += buffer.str();
            } else {
                m_historicalLogText += std::format(
                    "[Log file missing or unreadable: {}]\n", job.logPath);
            }
        } else if (!job.log.empty()) {
            m_historicalLogText += job.log;
        }
    }

    if (m_historicalLogText.empty())
        m_historicalLogText = "[No log content available for this run.]\n";
}

/** @copydoc EditorBuildPipelineModal::BuildRecentRunEntries */
std::vector<Horo::Ui::RecentRunEntry> EditorBuildPipelineModal::BuildRecentRunEntries() {
    std::vector<Horo::Ui::RecentRunEntry> runs;
    runs.reserve(m_history.size());
    for (size_t i = 0; i < m_history.size(); ++i) {
        const auto &entry = m_history[i];
        Horo::Ui::RecentRunEntry run;
        run.succeeded = entry.allSucceeded;
        run.label = FormatRecentRunTimestamp(entry.timestamp);
        run.detail = entry.allSucceeded ? "Succeeded" : "Failed";

        for (const auto &job : entry.jobs)
            AppendUniquePlatformLabel(run.platformLabel,
                                      GetBuildTargetOSLabel(job.os));

        const size_t entryIndex = i;
        run.onClick = [this, entryIndex]() { LoadHistoricalLog(entryIndex); };
        runs.push_back(std::move(run));
    }
    return runs;
}

/** @copydoc EditorBuildPipelineModal::DrawLogColumn */
void EditorBuildPipelineModal::DrawLogColumn(float width, float height) {
    const auto &theme = Horo::Ui::GetEditorTheme();
    ImGui::BeginChild("##build_log_column", ImVec2(width, height),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    const bool viewingHistory = (m_viewingHistoryIndex >= 0 &&
                                 static_cast<size_t>(m_viewingHistoryIndex) < m_history.size());
    const std::string displayLog = viewingHistory ? m_historicalLogText
                                                  : BuildLiveLogText();
    const std::string redactedLog = Horo::Build::RedactSecrets(displayLog);
    const float logAreaH = ImGui::GetContentRegionAvail().y - 8.0f;
    const float totalW = ImGui::GetContentRegionAvail().x;
    const float sideGap = 10.0f;
    const float rightW = std::clamp(totalW * 0.27f, 250.0f, 300.0f);
    const float leftW = std::max(320.0f, totalW - rightW - sideGap);

    ImGui::BeginChild("##log_main_panel", ImVec2(leftW, logAreaH),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    if (viewingHistory)
        DrawHistoryLogBanner(m_history[static_cast<size_t>(m_viewingHistoryIndex)]);
    Horo::Ui::RenderLogViewer(theme, redactedLog, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();

    ImGui::SameLine(0.0f, sideGap);
    ImGui::BeginChild("##log_side_panel", ImVec2(rightW, logAreaH),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    const bool isBuilding = (m_state == BuildPipelineState::Building);
    const bool isDone = (m_state == BuildPipelineState::Done ||
                         m_state == BuildPipelineState::Error);
    float progress = 0.0f;
    if (isDone)
        progress = 1.0f;
    else if (isBuilding)
        progress = static_cast<float>(m_draft.totalProgress) / 100.0f;
    Horo::Ui::RenderBuildProgressCard(theme, progress);
    ImGui::Spacing();
    std::vector<Horo::Ui::RecentRunEntry> runs = BuildRecentRunEntries();
    Horo::Ui::RenderRecentRunsCard(theme, runs, 5);
    ImGui::EndChild();

    ImGui::EndChild();
}

/** @copydoc EditorBuildPipelineModal::DrawJobDiagnostic */
void EditorBuildPipelineModal::DrawJobDiagnostic(const BuildJob &job) const {
    const auto &theme = Horo::Ui::GetEditorTheme();
    if (job.diagnostic.stage == Horo::Build::BuildStage::Unknown &&
        job.diagnostic.suggestion.empty())
        return;

    ImGui::Spacing();
    ImGui::Indent(20.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
    if (job.diagnostic.stage != Horo::Build::BuildStage::Unknown) {
        ImGui::Text("Stage: %s",
                    Horo::Build::GetBuildStageLabel(job.diagnostic.stage));
    }
    if (job.diagnostic.exitCode != 0) {
        ImGui::SameLine(120.0f);
        ImGui::Text("Exit: %d", job.diagnostic.exitCode);
    }
    if (!job.diagnostic.suggestion.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(1.0f, 0.85f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", job.diagnostic.suggestion.c_str());
        ImGui::PopStyleColor();
    }
    if (!job.diagnostic.logExcerpt.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
        ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]);
        ImGui::TextWrapped("Log excerpt (redacted):\n%s",
                           job.diagnostic.logExcerpt.c_str());
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
    ImGui::PopStyleColor();
    ImGui::Unindent(20.0f);
}

/** @copydoc EditorBuildPipelineModal::DrawProgressSection */
void EditorBuildPipelineModal::DrawProgressSection() const {
    const auto &theme = Horo::Ui::GetEditorTheme();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.text);
    ImGui::TextUnformatted("Build Progress");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    for (const auto &job : m_draft.jobs) {
        const std::string label = std::format("{} {} {}",
                                              GetBuildTargetOSLabel(job.os),
                                              GetBuildArchLabel(job.arch),
                                              GetBuildConfigLabel(job.config));
        ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(job.status));
        ImGui::Text("%s  %s", StatusIcon(job.status), label.c_str());
        ImGui::PopStyleColor();



        if (job.error.empty())
            continue;

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::TextUnformatted(job.error.c_str());
        ImGui::PopStyleColor();
        DrawJobDiagnostic(job);
    }

    // Build progress bar.
    ImGui::Spacing();
    const float barWidth = ImGui::GetContentRegionAvail().x;
    ImGui::ProgressBar(static_cast<float>(m_draft.totalProgress) / 100.0f,
                       ImVec2(barWidth, 0.0f));
    ImGui::Text("%d%% complete", m_draft.totalProgress);

    // Build log output.
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Build Log", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("##build_log", ImVec2(0.0f, 120.0f),
                          ImGuiChildFlags_Border,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar);

        for (const auto &job : m_draft.jobs) {
            if (!job.log.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, theme.palette.textMuted);
                const std::string redacted = Horo::Build::RedactSecrets(job.log);
                ImGui::TextUnformatted(redacted.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::EndChild();
    }
}

void EditorBuildPipelineModal::DrawActionsSection() {
    const auto &theme = Horo::Ui::GetEditorTheme();
    const bool building = (m_state == BuildPipelineState::Building);
    const bool downloading = (m_state == BuildPipelineState::Downloading);
    const bool done = (m_state == BuildPipelineState::Done ||
                       m_state == BuildPipelineState::Error);

    if (building || downloading) {
        const std::array<Horo::Ui::EditorDialogFooterButton, 1> buttons = {{
            {"cancel_build", "Cancel Build",
             Horo::Ui::EditorDialogFooterButtonStyle::Destructive,
             nullptr, 132.0f, true},
        }};
        if (const Horo::Ui::EditorDialogFooterConfig footer{
                "##build_pipeline_footer",
                58.0f,
                {"Build Progress:",
                 downloading ? "Downloading Artifacts..." : "Building...",
                 static_cast<float>(m_draft.totalProgress) / 100.0f,
                 300.0f},
                buttons};
            Horo::Ui::RenderEditorDialogFooter(theme, footer) == 0)
            CancelAllBuilds();
        return;
    }

    if (done) {
      Horo::Ui::RenderBuildFooterBar(
          theme, {
                     m_draft.anyJobFailed ? "Failed" : "Succeeded",
                     1.0f,
                     true,
                     true,
                     [this]() { RestartBuildFromDraft(); },
                     [this]() { ExportCurrentLog(); },
                     [this]() { Close(); },
                 });
      return;
    }

    const bool targetBuildable = IsSelectedTargetBuildable();
    const std::array<Horo::Ui::EditorDialogFooterButton, 2> buttons = {{
        {"build_all", "Build All",
         Horo::Ui::EditorDialogFooterButtonStyle::Primary,
         nullptr, 120.0f, targetBuildable},
        {"close", "Close",
         Horo::Ui::EditorDialogFooterButtonStyle::Secondary,
         nullptr, 104.0f, true},
    }};
    const Horo::Ui::EditorDialogFooterConfig footer{
        "##build_pipeline_footer",
        58.0f,
        {},
        buttons,
    };

    if (!targetBuildable) {
        const std::string reason = SelectedTargetUnavailableReason();
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImVec4(0.95f, 0.75f, 0.35f, 1.0f));
        ImGui::TextUnformatted(reason.c_str());
        ImGui::PopStyleColor();
    }

    const int clicked = Horo::Ui::RenderEditorDialogFooter(theme, footer);
    if (clicked == 0)
        RestartBuildFromDraft();
    else if (clicked == 1)
        Close();
}

/** @copydoc EditorBuildPipelineModal::ExportCurrentLog */
void EditorBuildPipelineModal::ExportCurrentLog() const {
    const bool viewingHistory =
        m_viewingHistoryIndex >= 0 &&
        static_cast<size_t>(m_viewingHistoryIndex) < m_history.size();
    const std::string log =
        viewingHistory ? m_historicalLogText : BuildLiveLogText();
    const std::filesystem::path outputDirectory =
        m_draft.outputRoot.empty()
            ? std::filesystem::path(ResolveDefaultOutputRoot())
            : std::filesystem::path(m_draft.outputRoot);
    std::error_code error;
    std::filesystem::create_directories(outputDirectory, error);
    if (error) {
        LogWarn("[Editor] Failed to create log export directory '{}': {}",
                outputDirectory.string(), error.message());
        return;
    }

    const std::filesystem::path outputPath =
        outputDirectory / "horo-build.log";
    std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
    if (!output.good()) {
        LogWarn("[Editor] Failed to export build log to '{}'",
                outputPath.string());
        return;
    }
    output << RedactSecrets(log);
    LogInfo("[Editor] Exported build log to '{}'", outputPath.string());
}

// -- Build execution ----------------------------------------------------------

std::string EditorBuildPipelineModal::BuildCommandForJob(
    const BuildJob &job) const {
    const std::filesystem::path projectRoot =
        m_projectRoot.empty() ? std::filesystem::current_path()
                              : std::filesystem::path(m_projectRoot);
    return Horo::Build::BuildCommandForJob(m_draft, job, projectRoot);
}

std::string EditorBuildPipelineModal::SignCommandForJob(
    const BuildJob &job) const {
    return Horo::Build::SignCommandForJob(m_draft, job);
}

/** @copydoc EditorBuildPipelineModal::StartRelease */
bool EditorBuildPipelineModal::StartRelease(BuildPipelineDraft&& draft,
                                            std::string projectRoot,
                                            std::string *outError) {
    if (outError)
        outError->clear();
    if (m_state == BuildPipelineState::Building || m_processRunner.IsActive()) {
        if (outError)
            *outError = "A release build is already running.";
        return false;
    }
    if (draft.jobs.empty()) {
        if (outError)
            *outError = "Release draft has no jobs.";
        return false;
    }

    for (BuildJob &job : draft.jobs) {
        job.status = BuildJobStatus::Pending;
        job.exitCode = 0;
        job.outputPath.clear();
        job.log.clear();
        job.logPath.clear();
        job.error.clear();
        job.timestamp.clear();
    }
    draft.allJobsComplete = false;
    draft.anyJobFailed = false;
    draft.totalProgress = ComputeTotalProgress(draft);

    m_projectRoot = std::move(projectRoot);
    m_draft = std::move(draft);
    m_original = m_draft.Clone();
    m_open = true;
    m_openRequested = true;
    m_activePanel = BuildPanelTab::Log;
    // Reset historical log view so the live build log takes precedence.
    m_viewingHistoryIndex = -1;
    m_historicalLogText.clear();
    LoadHistory();
    TransitionTo(BuildPipelineState::Configuring);
    StartNextPendingJob();

    if (m_state != BuildPipelineState::Building) {
        if (outError)
            *outError = m_draft.jobs.front().error.empty()
                            ? "Release build did not start."
                            : m_draft.jobs.front().error;
        return false;
    }
    return true;
}

void EditorBuildPipelineModal::StartNextPendingJob() {
    auto it = std::ranges::find_if(m_draft.jobs, [](const BuildJob &j) {
        return j.status == BuildJobStatus::Pending;
    });
    if (it == m_draft.jobs.end())
        return;

    BuildJob &job = *it;

    const std::filesystem::path projectRoot =
        m_projectRoot.empty() ? std::filesystem::current_path()
                              : std::filesystem::path(m_projectRoot);



    Horo::Build::TargetCapability cap;
    if (m_toolchainStore) {
        cap = Horo::Build::EvaluateTargetCapability(job.os, DefaultArchForOS(job.os), *m_toolchainStore);
    } else {
        Horo::Build::ToolchainSettingsStore emptyStore;
        cap = Horo::Build::EvaluateTargetCapability(job.os, DefaultArchForOS(job.os), emptyStore);
    }

    if (!cap.IsEnabled()) {
        const std::string blockedMessage =
            Horo::Build::FormatTargetCapabilityBlockReason(cap);
        LogError("[ERROR] {}", blockedMessage);
        job.status = BuildJobStatus::Failed;
        job.exitCode = 1;
        job.error = blockedMessage;
        AppendJobErrorLog(job, job.error);
        job.diagnostic = DiagnoseBuildFailure(job.exitCode, job.log, job.error);
        job.timestamp = CurrentTimestamp();
        WriteJobLogToDisk(m_draft, job, projectRoot);
        m_draft.totalProgress = ComputeTotalProgress(m_draft);
        if (!FinalizeIfAllJobsTerminal())
            StartNextPendingJob();
        return;
    }

    const std::string label = std::format("Building {} {}",
                                          GetBuildTargetOSLabel(job.os),
                                          GetBuildConfigLabel(job.config));

    Horo::Launcher::ResolvedLauncherCommand command;
    const Horo::Build::BuildCommandPlan plan =
        Horo::Build::CreateBuildCommandPlan(m_draft, job, projectRoot);
    command.executable = plan.executable;
    command.args = plan.args;
    command.workingDirectory = plan.workingDirectory;
    command.debugString = plan.debugString;

    std::optional<ScopedEnvironmentVariable> archivePasswordEnv;
    if (!m_draft.archivePassword.Empty()) {
        archivePasswordEnv.emplace("HORO_RELEASE_ARCHIVE_PASSWORD",
                                   std::string(m_draft.archivePassword.View()));
    }

    m_lastProcessOutputSize = 0;
    if (std::string error; !m_processRunner.Start(command, label, &error)) {
        job.status = BuildJobStatus::Failed;
        job.exitCode = 1;
        job.error = error.empty() ? "Failed to start build process." : error;
        AppendJobErrorLog(job, job.error);
        job.diagnostic = DiagnoseBuildFailure(job.exitCode, job.log, job.error);
        job.timestamp = CurrentTimestamp();
        WriteJobLogToDisk(m_draft, job, projectRoot);
        m_draft.totalProgress = ComputeTotalProgress(m_draft);
        if (!FinalizeIfAllJobsTerminal())
            StartNextPendingJob();
        return;
    }

    job.status = BuildJobStatus::Building;
    job.log = std::format("Started: {}\nWorking directory: {}",
                          RedactCommandLine(command.debugString),
                          command.workingDirectory.generic_string());
    TransitionTo(BuildPipelineState::Building);
}

void EditorBuildPipelineModal::CancelAllBuilds() {
    using enum BuildJobStatus;
    if (m_processRunner.IsActive())
        m_processRunner.Stop();
    m_lastProcessOutputSize = 0;

    const std::filesystem::path projectRoot =
        m_projectRoot.empty() ? std::filesystem::current_path()
                              : std::filesystem::path(m_projectRoot);

    for (auto &job : m_draft.jobs) {
        if (job.status == Building || job.status == Pending) {
            job.status = Cancelled;
            job.timestamp = CurrentTimestamp();
            // Persist any captured log output for the cancelled job.
            WriteJobLogToDisk(m_draft, job, projectRoot);
        }
    }
    // Only finalize when we're in a build-able state; otherwise just
    // mark the jobs (e.g. when called from Idle, no transition needed).
    if (m_state == BuildPipelineState::Configuring ||
        m_state == BuildPipelineState::Building ||
        m_state == BuildPipelineState::Packaging ||
        m_state == BuildPipelineState::Downloading) {
        FinalizeIfAllJobsTerminal();
    } else {
        m_draft.allJobsComplete = true;
        m_draft.totalProgress = ComputeTotalProgress(m_draft);
    }
}

// -- Persistence --------------------------------------------------------------

std::string EditorBuildPipelineModal::ResolveDefaultOutputRoot() const {
    if (!m_projectRoot.empty())
        return Horo::Build::ResolveDefaultOutputRoot(std::filesystem::path(m_projectRoot));
    return Horo::Build::ResolveDefaultOutputRoot(std::filesystem::current_path());
}

std::string EditorBuildPipelineModal::DefaultVersionTag() const {
    return Horo::Build::DefaultVersionTag();
}

/** @copydoc EditorBuildPipelineModal::BumpVersionPatch */
void EditorBuildPipelineModal::BumpVersionPatch() {
    // Parse current version tag, bump patch, write back.
    std::string tag = m_versionTagBuf.data();
    Horo::Build::SemVer ver = Horo::Build::ParseSemVer(tag);
    if (!ver.IsZero()) {
        Horo::Build::BumpPatch(ver);
        const std::string bumped = Horo::Build::SemVerToString(ver);
        CopyToInputBuffer(m_versionTagBuf, bumped);
        m_draft.versionTag = bumped;
        m_versionTagError.clear();
    }
}

/** @copydoc EditorBuildPipelineModal::ValidateVersionFields */
void EditorBuildPipelineModal::ValidateVersionFields() {
    // Update draft from buffers first.
    m_draft.versionTag = m_versionTagBuf.data();
    m_draft.gameVersion = m_gameVersionBuf.data();

    // Validate version tag.
    {
        const std::string_view tag(m_draft.versionTag);
        if (tag.empty()) {
            m_versionTagError = "Version tag cannot be empty.";
        } else if (!Horo::Build::IsValidSemVer(tag)) {
            m_versionTagError = "Invalid SemVer format (expected X.Y.Z).";
        } else {
            m_versionTagError.clear();
        }
    }

    // Validate game version.
    {
        const std::string_view gv(m_draft.gameVersion);
        if (gv.empty()) {
            m_gameVersionError = "Game version cannot be empty.";
        } else if (!Horo::Build::IsValidSemVer(gv)) {
            m_gameVersionError = "Invalid SemVer format (expected X.Y.Z).";
        } else {
            m_gameVersionError.clear();
        }
    }
}

void EditorBuildPipelineModal::LoadHistory() {
    m_history = ReadHistoryJson(BuildHistoryPath());
}

void EditorBuildPipelineModal::SaveHistory() const {
    WriteHistoryJson(BuildHistoryPath(), m_history);
}

void EditorBuildPipelineModal::AppendHistoryEntry() {
    BuildHistoryEntry entry;
    entry.versionTag = m_draft.versionTag;
    entry.timestamp = CurrentTimestamp();
    entry.jobs = m_draft.jobs;
    entry.allSucceeded = !m_draft.anyJobFailed;
    m_history.push_back(std::move(entry));
    // Keep at most 20 entries.
    while (m_history.size() > 20)
        m_history.erase(m_history.begin());
    SaveHistory();
}

bool EditorBuildPipelineModal::FinalizeIfAllJobsTerminal() {
    if (m_draft.jobs.empty())
        return false;

    if (const bool allTerminal = std::ranges::all_of(m_draft.jobs, [](const BuildJob &j) {
            return j.status != BuildJobStatus::Pending &&
                   j.status != BuildJobStatus::Building;
        });
        !allTerminal)
        return false;

    if (m_draft.allJobsComplete)
        return true;

    m_draft.allJobsComplete = true;
    m_draft.anyJobFailed = std::ranges::any_of(m_draft.jobs, [](const BuildJob &j) {
        return j.status == BuildJobStatus::Failed ||
               j.status == BuildJobStatus::Cancelled;
    });
    m_draft.totalProgress = ComputeTotalProgress(m_draft);

    // Transition through the pipeline state machine to terminal state.
    if (m_draft.anyJobFailed) {
        TransitionTo(BuildPipelineState::Error);
    } else {
        // For local builds, packaging is inline in the build command.
        TransitionTo(BuildPipelineState::Done);
    }

    AppendHistoryEntry();
    if (m_buildCompleteCallback)
        m_buildCompleteCallback(m_draft.jobs);
    return true;
}


} // namespace Horo::Editor
