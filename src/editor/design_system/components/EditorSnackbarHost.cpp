#include "Horo/Editor/EditorSnackbarHost.h"
#include "Horo/Editor/EditorTheme.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Horo::Editor
{
namespace {
    constexpr float kCardWidth          = 360.0f;
    constexpr float kOuterMarginRight   = 24.0f;
    constexpr float kOuterMarginBottom  = 24.0f;
    constexpr float kCardGap            = 10.0f;
    constexpr float kCardPaddingX       = 16.0f;
    constexpr float kCardPaddingTop     = 14.0f;
    constexpr float kCardPaddingBottom  = 14.0f;
    constexpr float kIconDiameter       = 26.0f;
    constexpr float kIconTextGap        = 12.0f;
    constexpr float kCornerRadius       = 10.0f;
    constexpr float kButtonRounding     = 8.0f;
    constexpr float kMinCardHeight      = 54.0f;
    constexpr std::size_t kMaxVisibleSnackbars = 3;

    enum class IconKind { Cross, Exclaim, Info, Check };

    struct SeverityStyle
    {
        ImU32 accent;
        ImU32 iconBg;
        IconKind icon;
    };

    [[nodiscard]] SeverityStyle GetSeverityStyle(const NotificationSeverity severity) noexcept
    {
        switch (severity) {
            case NotificationSeverity::Error:
                return { IM_COL32(248, 113, 113, 255), IM_COL32(248, 113, 113, 38), IconKind::Cross };
            case NotificationSeverity::Warning:
                return { IM_COL32(251, 191, 36, 255), IM_COL32(251, 191, 36, 38), IconKind::Exclaim };
            case NotificationSeverity::Info:
                return { IM_COL32(56, 189, 248, 255), IM_COL32(56, 189, 248, 38), IconKind::Info };
            case NotificationSeverity::Success:
                return { IM_COL32(74, 222, 128, 255), IM_COL32(74, 222, 128, 38), IconKind::Check };
        }
        return { IM_COL32(56, 189, 248, 255), IM_COL32(56, 189, 248, 38), IconKind::Info };
    }

    // Drawn with primitives instead of font glyphs so it never depends on the
    // active font supporting a given unicode symbol (that's what was showing
    // up as "?" tofu boxes for the ✕ / ✓ characters).
    void DrawSeverityIcon(ImDrawList *drawList, const ImVec2 &center, const IconKind icon, const ImU32 color)
    {
        constexpr float r = 4.5f;
        constexpr float thickness = 1.6f;
        switch (icon) {
            case IconKind::Cross:
                drawList->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), color, thickness);
                drawList->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), color, thickness);
                break;
            case IconKind::Exclaim:
                drawList->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r * 0.35f), color, thickness);
                drawList->AddCircleFilled(ImVec2(center.x, center.y + r), 1.3f, color, 8);
                break;
            case IconKind::Info:
                drawList->AddCircleFilled(ImVec2(center.x, center.y - r * 0.6f), 1.3f, color, 8);
                drawList->AddLine(ImVec2(center.x, center.y - r * 0.05f), ImVec2(center.x, center.y + r), color, thickness);
                break;
            case IconKind::Check:
                drawList->AddLine(ImVec2(center.x - r, center.y + r * 0.1f), ImVec2(center.x - r * 0.15f, center.y + r), color, thickness);
                drawList->AddLine(ImVec2(center.x - r * 0.15f, center.y + r), ImVec2(center.x + r, center.y - r * 0.7f), color, thickness);
                break;
        }
    }

    // Small font-independent X, used for the dismiss control.
    void DrawCrossIcon(ImDrawList *drawList, const ImVec2 &center, const float r, const ImU32 color, const float thickness)
    {
        drawList->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), color, thickness);
        drawList->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), color, thickness);
    }

    [[nodiscard]] float TextBlockWidth(const bool hasActions, const bool dismissible) noexcept
    {
        const float rightControls = (dismissible ? 26.0f : 0.0f) + (hasActions ? 84.0f : 0.0f);
        return kCardWidth - (kCardPaddingX * 2.0f) - kIconDiameter - kIconTextGap - rightControls;
    }

    // Measures the height a card will need *before* we open the overlay window,
    // so window size/position never depends on a previous frame's cached size.
    [[nodiscard]] float MeasureCardHeight(const ActiveSnackbar &item) noexcept
    {
        const float wrapWidth = TextBlockWidth(!item.event.actions.empty(), item.event.dismissible);
        float textHeight = ImGui::CalcTextSize(item.event.message.c_str(), nullptr, false, wrapWidth).y;
        if (!item.event.title.empty()) {
            textHeight += ImGui::CalcTextSize(item.event.title.c_str(), nullptr, false, wrapWidth).y;
        }
        return std::max(kMinCardHeight, textHeight + kCardPaddingTop + kCardPaddingBottom);
    }
} // namespace

EditorSnackbarHost::EditorSnackbarHost(EditorDataBus &events)
    : events_(&events)
{
    if (events_ != nullptr) {
        subscription_ = events_->Subscribe<NotificationEvent>(
            [this](const NotificationEvent &event) { OnNotificationEvent(event); });
    }
}

EditorSnackbarHost::~EditorSnackbarHost()
{
    Clear();
}

void EditorSnackbarHost::Clear()
{
    activeSnackbars_.clear();
}

void EditorSnackbarHost::OnNotificationEvent(const NotificationEvent &event)
{
    // NOTE: dedup only kicks in when the caller supplies a deduplicationKey.
    // If a notification appears to "double up" visually, the most likely cause
    // is the emitting call site not setting this key for repeated triggers of
    // the same logical message (e.g. re-validating on every Play attempt).
    if (!event.deduplicationKey.empty()) {
        for (ActiveSnackbar &item : activeSnackbars_) {
            if (item.event.deduplicationKey == event.deduplicationKey) {
                item.event.message = event.message;
                if (!event.title.empty())
                    item.event.title = event.title;
                if (!event.actions.empty())
                    item.event.actions = event.actions;
                item.event.severity = event.severity;
                item.event.durationSeconds = event.durationSeconds > 0.0f ? event.durationSeconds : 8.0f;
                item.repeatCount++;
                item.elapsedSeconds = 0.0f;
                return;
            }
        }
    }

    NotificationEvent copy = event;
    if (copy.durationSeconds <= 0.0f)
        copy.durationSeconds = 8.0f;

    activeSnackbars_.push_back(ActiveSnackbar{.event = std::move(copy), .elapsedSeconds = 0.0f, .repeatCount = 1});
}

void EditorSnackbarHost::DismissAt(const std::size_t index)
{
    if (index < activeSnackbars_.size()) {
        activeSnackbars_.erase(activeSnackbars_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

std::optional<SnackbarActionInvokedEvent> EditorSnackbarHost::Draw(const EditorGuiContext &, const float elapsedSeconds)
{
    if (activeSnackbars_.empty())
        return std::nullopt;

    // Process timers
    if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0f) {
        for (std::size_t i = 0; i < activeSnackbars_.size();) {
            ActiveSnackbar &item = activeSnackbars_[i];
            item.elapsedSeconds += elapsedSeconds;
            if (item.event.durationSeconds > 0.0f && item.elapsedSeconds >= item.event.durationSeconds) {
                DismissAt(i);
            } else {
                ++i;
            }
        }
    }

    if (activeSnackbars_.empty())
        return std::nullopt;

    // Esc key dismiss top active snackbar if dismissible
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (!activeSnackbars_.empty() && activeSnackbars_.front().event.dismissible) {
            DismissAt(0);
            if (activeSnackbars_.empty())
                return std::nullopt;
        }
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 viewportSize = viewport->WorkSize;
    const ImVec2 viewportPos = viewport->WorkPos;

    const std::size_t visibleCount = std::min(activeSnackbars_.size(), kMaxVisibleSnackbars);

    // --- Pre-pass: measure exact content height for THIS frame -------------
    // This is the fix for the "toast appears in the middle of the screen" bug:
    // the old code used ImGuiWindowFlags_AlwaysAutoResize together with a
    // bottom pivot in SetNextWindowPos. That pivot math is applied using the
    // *previous* frame's window size, which is wrong/zero the first time a
    // card appears or whenever the stack's content changes size — so the
    // window gets placed using a stale height instead of the real one.
    // Measuring here and calling SetNextWindowSize explicitly removes that
    // dependency entirely: position is correct on frame one, every time.
    float totalHeight = 0.0f;
    for (std::size_t idx = 0; idx < visibleCount; ++idx) {
        totalHeight += MeasureCardHeight(activeSnackbars_[idx]);
        if (idx + 1 < visibleCount)
            totalHeight += kCardGap;
    }

    const float posX = viewportPos.x + viewportSize.x - kCardWidth - kOuterMarginRight;
    const float bottomY = viewportPos.y + viewportSize.y - kOuterMarginBottom;
    const float topY = bottomY - totalHeight;

    ImGui::SetNextWindowPos(ImVec2(posX, topY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kCardWidth, totalHeight), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBackground;

    std::optional<SnackbarActionInvokedEvent> result;

    if (ImGui::Begin("##EditorSnackbarOverlay", nullptr, windowFlags)) {
        ImDrawList *drawList = ImGui::GetWindowDrawList();

        for (std::size_t idx = 0; idx < visibleCount; ++idx) {
            ActiveSnackbar &snackbar = activeSnackbars_[idx];
            ImGui::PushID(static_cast<int>(snackbar.event.id));

            const SeverityStyle style = GetSeverityStyle(snackbar.event.severity);
            const float cardHeight = MeasureCardHeight(snackbar);
            const ImVec2 pMin = ImGui::GetCursorScreenPos();
            const ImVec2 pMax = ImVec2(pMin.x + kCardWidth, pMin.y + cardHeight);

            // Channels: 0 = shadow + card background, 1 = icon/text/buttons
            drawList->ChannelsSplit(2);
            drawList->ChannelsSetCurrent(1);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.20f));

            ImGui::BeginGroup();

            // Icon badge
            const ImVec2 iconCenter(pMin.x + kCardPaddingX + kIconDiameter * 0.5f, pMin.y + kCardPaddingTop + kIconDiameter * 0.5f);
            drawList->AddCircleFilled(iconCenter, kIconDiameter * 0.5f, style.iconBg, 20);
            DrawSeverityIcon(drawList, iconCenter, style.icon, style.accent);

            const float textX = pMin.x + kCardPaddingX + kIconDiameter + kIconTextGap;
            const float textWrapWidth = TextBlockWidth(!snackbar.event.actions.empty(), snackbar.event.dismissible);

            ImGui::SetCursorScreenPos(ImVec2(textX, pMin.y + kCardPaddingTop));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(textX + textWrapWidth);

            if (!snackbar.event.title.empty()) {
                ImGui::TextColored(ImVec4(0.97f, 0.97f, 1.0f, 1.0f), "%s", snackbar.event.title.c_str());
                if (snackbar.repeatCount > 1) {
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextDisabled("x%zu", snackbar.repeatCount);
                }
                ImGui::TextColored(ImVec4(0.72f, 0.76f, 0.83f, 1.0f), "%s", snackbar.event.message.c_str());
            } else {
                ImGui::TextColored(ImVec4(0.94f, 0.95f, 0.98f, 1.0f), "%s", snackbar.event.message.c_str());
                if (snackbar.repeatCount > 1) {
                    ImGui::SameLine(0.0f, 6.0f);
                    ImGui::TextDisabled("x%zu", snackbar.repeatCount);
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();

            // Actions & close button, right-aligned, vertically centered on the icon row
            const float controlsY = pMin.y + kCardPaddingTop - 3.0f;
            float controlsX = pMax.x - kCardPaddingX;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kButtonRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));

            if (snackbar.event.dismissible) {
                constexpr float kCloseBtnSize = 22.0f;
                controlsX -= kCloseBtnSize;
                const float closeTopY = iconCenter.y - kCloseBtnSize * 0.5f;
                ImGui::SetCursorScreenPos(ImVec2(controlsX, closeTopY));
                ImGui::InvisibleButton("##dismiss", ImVec2(kCloseBtnSize, kCloseBtnSize));
                const ImVec2 closeCenter(controlsX + kCloseBtnSize * 0.5f, iconCenter.y);
                if (ImGui::IsItemHovered()) {
                    drawList->AddRectFilled(ImVec2(closeCenter.x - 11.0f, closeCenter.y - 11.0f),
                                            ImVec2(closeCenter.x + 11.0f, closeCenter.y + 11.0f),
                                            IM_COL32(255, 255, 255, 20), 6.0f);
                }
                DrawCrossIcon(drawList, closeCenter, 4.5f, IM_COL32(158, 166, 178, 255), 1.4f);
                if (ImGui::IsItemClicked()) {
                    ImGui::PopStyleVar(2);
                    ImGui::EndGroup();
                    ImGui::PopStyleColor(3);
                    drawList->ChannelsMerge();
                    ImGui::PopID();
                    DismissAt(idx);
                    goto end_draw_loop;
                }
            }

            if (!snackbar.event.actions.empty()) {
                for (auto actionIt = snackbar.event.actions.rbegin(); actionIt != snackbar.event.actions.rend(); ++actionIt) {
                    const NotificationAction &action = *actionIt;
                    const float btnWidth = ImGui::CalcTextSize(action.label.c_str()).x + 20.0f;
                    controlsX -= (btnWidth + 6.0f);
                    ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));

                    const ImVec4 accentF = ImColor(style.accent).Value;
                    ImGui::PushStyleColor(ImGuiCol_Button, ImColor(style.iconBg).Value);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accentF.x, accentF.y, accentF.z, 0.28f));
                    ImGui::PushStyleColor(ImGuiCol_Text, accentF);
                    if (ImGui::Button(action.label.c_str())) {
                        result = SnackbarActionInvokedEvent{
                            .source = snackbar.event.source,
                            .actionId = action.actionId,
                            .notificationId = snackbar.event.id,
                        };
                        ImGui::PopStyleColor(3);
                        ImGui::PopStyleVar(2);
                        ImGui::EndGroup();
                        ImGui::PopStyleColor(3);
                        drawList->ChannelsMerge();
                        ImGui::PopID();
                        goto end_draw_loop;
                    }
                    ImGui::PopStyleColor(3);
                }
            }

            ImGui::PopStyleVar(2);
            ImGui::EndGroup();
            ImGui::PopStyleColor(3); // Buttons

            // Channel 0: shadow + card background, behind icon/text/buttons
            drawList->ChannelsSetCurrent(0);

            // Soft elevation shadow
            drawList->AddRectFilled(ImVec2(pMin.x + 3.0f, pMin.y + 5.0f), ImVec2(pMax.x + 3.0f, pMax.y + 7.0f),
                                    IM_COL32(0, 0, 0, 90), kCornerRadius);

            drawList->AddRectFilled(pMin, pMax, IM_COL32(26, 28, 36, 255), kCornerRadius);
            drawList->AddRect(pMin, pMax, IM_COL32(255, 255, 255, 14), kCornerRadius, 0, 1.0f);
            // Subtle 1px top highlight for a soft "glass" edge
            drawList->AddLine(ImVec2(pMin.x + kCornerRadius, pMin.y + 0.5f), ImVec2(pMax.x - kCornerRadius, pMin.y + 0.5f),
                              IM_COL32(255, 255, 255, 22), 1.0f);

            // Bottom progress track + fill
            if (snackbar.event.durationSeconds > 0.0f) {
                const float fraction = std::clamp(1.0f - (snackbar.elapsedSeconds / snackbar.event.durationSeconds), 0.0f, 1.0f);
                const float trackY = pMax.y - 5.0f;
                const ImVec2 trackMin(pMin.x + kCardPaddingX, trackY);
                const ImVec2 trackMax(pMax.x - kCardPaddingX, trackY + 2.5f);
                drawList->AddRectFilled(trackMin, trackMax, IM_COL32(255, 255, 255, 18), 2.0f);
                const ImVec2 fillMax(trackMin.x + (trackMax.x - trackMin.x) * fraction, trackMax.y);
                drawList->AddRectFilled(trackMin, fillMax, style.accent, 2.0f);
            }

            drawList->ChannelsMerge();

            ImGui::SetCursorScreenPos(ImVec2(pMin.x, pMax.y + kCardGap));
            ImGui::PopID();
        }
    }

end_draw_loop:
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    return result;
}
} // namespace Horo::Editor