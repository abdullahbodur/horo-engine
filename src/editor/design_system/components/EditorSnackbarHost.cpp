/** @copydoc EditorSnackbarHost.h */

#include "Horo/Editor/EditorSnackbarHost.h"

#include "Horo/Editor/EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace Horo::Editor {
    namespace {
        constexpr float kCardWidth = 360.0f;
        constexpr float kOuterMarginRight = 24.0f;
        constexpr float kOuterMarginBottom = 32.0f;
        constexpr float kCardGap = 10.0f;
        constexpr float kCardPaddingX = 16.0f;
        constexpr float kCardPaddingTop = 14.0f;
        constexpr float kCardPaddingBottom = 14.0f;
        constexpr float kIconDiameter = 26.0f;
        constexpr float kIconTextGap = 12.0f;
        constexpr float kCornerRadius = 10.0f;
        constexpr float kButtonRounding = 8.0f;
        constexpr float kMinCardHeight = 54.0f;
        constexpr float kTextLineGap = 3.0f;
        constexpr float kShadowExtent = 8.0f;
        constexpr float kProgressInsetX = 12.0f;
        constexpr float kProgressBottom = 6.0f;
        constexpr float kProgressHeight = 2.0f;
        constexpr std::size_t kMaxVisibleSnackbars = 3;
        constexpr std::size_t kMaxQueuedSnackbars = 64;

        enum class IconKind {
            Cross,
            Exclaim,
            Info,
            Check
        };

        enum class CardInteractionStatus {
            None,
            Dismissed,
            ActionTriggered
        };

        struct CardInteractionResult {
            CardInteractionStatus status{CardInteractionStatus::None};
            std::optional<SnackbarActionInvokedEvent> actionEvent;
        };

        struct SeverityStyle {
            ImU32 accent;
            ImU32 iconBg;
            IconKind icon;
        };

        [[nodiscard]] SeverityStyle GetSeverityStyle(const NotificationSeverity severity) noexcept {
            using enum NotificationSeverity;
            switch (severity) {
                case Error:
                    return {IM_COL32(248, 113, 113, 255), IM_COL32(248, 113, 113, 38), IconKind::Cross};
                case Warning:
                    return {IM_COL32(251, 191, 36, 255), IM_COL32(251, 191, 36, 38), IconKind::Exclaim};
                case Info:
                    return {IM_COL32(56, 189, 248, 255), IM_COL32(56, 189, 248, 38), IconKind::Info};
                case Success:
                    return {IM_COL32(74, 222, 128, 255), IM_COL32(74, 222, 128, 38), IconKind::Check};
            }
            return {IM_COL32(56, 189, 248, 255), IM_COL32(56, 189, 248, 38), IconKind::Info};
        }

        // Drawn with primitives instead of font glyphs so it never depends on the
        // active font supporting a given unicode symbol (that's what was showing
        // up as "?" tofu boxes for the ✕ / ✓ characters).
        void DrawSeverityIcon(ImDrawList *drawList, const ImVec2 &center, const IconKind icon, const ImU32 color) {
            constexpr float r = 4.5f;
            constexpr float thickness = 1.6f;
            using enum IconKind;
            switch (icon) {
                case Cross:
                    drawList->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), color, thickness);
                    drawList->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), color, thickness);
                    break;
                case Exclaim:
                    drawList->AddLine(ImVec2(center.x, center.y - r), ImVec2(center.x, center.y + r * 0.35f), color, thickness);
                    drawList->AddCircleFilled(ImVec2(center.x, center.y + r), 1.3f, color, 8);
                    break;
                case Info:
                    drawList->AddCircleFilled(ImVec2(center.x, center.y - r * 0.6f), 1.3f, color, 8);
                    drawList->AddLine(ImVec2(center.x, center.y - r * 0.05f), ImVec2(center.x, center.y + r), color, thickness);
                    break;
                case Check:
                    drawList->AddLine(ImVec2(center.x - r, center.y + r * 0.1f), ImVec2(center.x - r * 0.15f, center.y + r), color,
                                      thickness);
                    drawList->AddLine(ImVec2(center.x - r * 0.15f, center.y + r), ImVec2(center.x + r, center.y - r * 0.7f), color,
                                      thickness);
                    break;
            }
        }

        // Small font-independent X, used for the dismiss control.
        void DrawCrossIcon(ImDrawList *drawList, const ImVec2 &center, const float r, const ImU32 color, const float thickness) {
            drawList->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), color, thickness);
            drawList->AddLine(ImVec2(center.x - r, center.y + r), ImVec2(center.x + r, center.y - r), color, thickness);
        }

        [[nodiscard]] float RightControlsWidth(const NotificationEvent &event) noexcept {
            float width = event.dismissible ? 26.0f : 0.0f;
            for (const NotificationAction &action : event.actions) {
                width += ImGui::CalcTextSize(action.label.c_str()).x + 26.0f;
            }
            return width;
        }

        [[nodiscard]] float TextBlockWidth(const NotificationEvent &event) noexcept {
            return std::max(48.0f, kCardWidth - (kCardPaddingX * 2.0f) - kIconDiameter - kIconTextGap - RightControlsWidth(event));
        }

        // Measures the height a card will need *before* we open the overlay window,
        // so window size/position never depends on a previous frame's cached size.
        [[nodiscard]] float MeasureCardHeight(const ActiveSnackbar &item) noexcept {
            const float wrapWidth = TextBlockWidth(item.event);
            float textHeight = ImGui::CalcTextSize(item.event.message.c_str(), nullptr, false, wrapWidth).y;
            if (!item.event.title.empty()) {
                textHeight += ImGui::CalcTextSize(item.event.title.c_str(), nullptr, false, wrapWidth).y + kTextLineGap;
            }
            return std::max(kMinCardHeight, textHeight + kCardPaddingTop + kCardPaddingBottom);
        }

        void UpdateTimers(std::deque<ActiveSnackbar> &snackbars, const float elapsedSeconds) {
            if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f) {
                return;
            }
            for (std::size_t i = 0; i < snackbars.size();) {
                ActiveSnackbar &item = snackbars[i];
                item.elapsedSeconds += elapsedSeconds;
                if (item.event.durationSeconds > 0.0f && item.elapsedSeconds >= item.event.durationSeconds) {
                    snackbars.erase(snackbars.begin() + static_cast<std::ptrdiff_t>(i));
                } else {
                    ++i;
                }
            }
        }

        void EnforceQueueLimit(std::deque<ActiveSnackbar> &snackbars) {
            while (snackbars.size() > kMaxQueuedSnackbars) {
                const auto oldestTransient = std::ranges::find_if(snackbars, [](const ActiveSnackbar &item) {
                    return item.event.durationSeconds > 0.0f;
                });
                if (oldestTransient != snackbars.end()) {
                    snackbars.erase(oldestTransient);
                } else {
                    snackbars.pop_front();
                }
            }
        }

        void DrawCardIconAndText(ImDrawList *drawList, const ActiveSnackbar &snackbar, const SeverityStyle &style, const ImVec2 &pMin,
                                 const ImVec2 &iconCenter) {
            drawList->AddCircleFilled(iconCenter, kIconDiameter * 0.5f, style.iconBg, 20);
            DrawSeverityIcon(drawList, iconCenter, style.icon, style.accent);

            const float textX = pMin.x + kCardPaddingX + kIconDiameter + kIconTextGap;
            const float textWrapWidth = TextBlockWidth(snackbar.event);

            ImGui::SetCursorScreenPos(ImVec2(textX, pMin.y + kCardPaddingTop));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, kTextLineGap));
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
            ImGui::PopStyleVar();
        }

        [[nodiscard]] bool DrawDismissButton(ImDrawList *drawList, const ImVec2 &iconCenter, float &controlsX) {
            constexpr float kCloseBtnSize = 22.0f;
            controlsX -= kCloseBtnSize;
            const float closeTopY = iconCenter.y - kCloseBtnSize * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(controlsX, closeTopY));
            ImGui::InvisibleButton("##dismiss", ImVec2(kCloseBtnSize, kCloseBtnSize));
            const ImVec2 closeCenter(controlsX + kCloseBtnSize * 0.5f, iconCenter.y);
            if (ImGui::IsItemHovered()) {
                drawList->AddRectFilled(ImVec2(closeCenter.x - 11.0f, closeCenter.y - 11.0f),
                                        ImVec2(closeCenter.x + 11.0f, closeCenter.y + 11.0f), IM_COL32(255, 255, 255, 20), 6.0f);
            }
            DrawCrossIcon(drawList, closeCenter, 4.5f, IM_COL32(158, 166, 178, 255), 1.4f);
            return ImGui::IsItemClicked();
        }

        [[nodiscard]] std::optional<SnackbarActionInvokedEvent> DrawActionButtons(const ActiveSnackbar &snackbar,
                                                                                  const SeverityStyle &style, const float controlsY,
                                                                                  float &controlsX) {
            for (auto actionIt = snackbar.event.actions.rbegin(); actionIt != snackbar.event.actions.rend(); ++actionIt) {
                const NotificationAction &action = *actionIt;
                const float btnWidth = ImGui::CalcTextSize(action.label.c_str()).x + 20.0f;
                controlsX -= (btnWidth + 6.0f);
                ImGui::SetCursorScreenPos(ImVec2(controlsX, controlsY));

                const ImVec4 accentF = ImColor(style.accent).Value;
                ImGui::PushStyleColor(ImGuiCol_Button, ImColor(style.iconBg).Value);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accentF.x, accentF.y, accentF.z, 0.28f));
                ImGui::PushStyleColor(ImGuiCol_Text, accentF);
                const bool clicked = ImGui::Button(action.label.c_str());
                ImGui::PopStyleColor(3);

                if (clicked) {
                    return SnackbarActionInvokedEvent{
                        .source = snackbar.event.source,
                        .actionId = action.actionId,
                        .notificationId = snackbar.event.id,
                    };
                }
            }
            return std::nullopt;
        }

        void DrawCardBackgroundAndProgress(ImDrawList *drawList, const ActiveSnackbar &snackbar, const SeverityStyle &style,
                                           const ImVec2 &pMin, const ImVec2 &pMax) {
            // Soft elevation shadow
            drawList->AddRectFilled(ImVec2(pMin.x + 3.0f, pMin.y + 5.0f), ImVec2(pMax.x + 3.0f, pMax.y + 7.0f), IM_COL32(0, 0, 0, 90),
                                    kCornerRadius);

            drawList->AddRectFilled(pMin, pMax, IM_COL32(26, 28, 36, 255), kCornerRadius);
            drawList->AddRect(pMin, pMax, IM_COL32(255, 255, 255, 14), kCornerRadius, 0, 1.0f);
            // Subtle 1px top highlight for a soft "glass" edge
            drawList->AddLine(ImVec2(pMin.x + kCornerRadius, pMin.y + 0.5f), ImVec2(pMax.x - kCornerRadius, pMin.y + 0.5f),
                              IM_COL32(255, 255, 255, 22), 1.0f);

            // Bottom progress track + fill
            if (snackbar.event.durationSeconds > 0.0f) {
                const float fraction = std::clamp(1.0f - (snackbar.elapsedSeconds / snackbar.event.durationSeconds), 0.0f, 1.0f);
                const float trackY = pMax.y - kProgressBottom;
                const ImVec2 trackMin(pMin.x + kProgressInsetX, trackY);
                const ImVec2 trackMax(pMax.x - kProgressInsetX, trackY + kProgressHeight);
                drawList->AddRectFilled(trackMin, trackMax, IM_COL32(255, 255, 255, 30), kProgressHeight * 0.5f);
                const ImVec2 fillMax(trackMin.x + (trackMax.x - trackMin.x) * fraction, trackMax.y);
                if (fillMax.x > trackMin.x) {
                    drawList->AddRectFilled(trackMin, fillMax, style.accent, kProgressHeight * 0.5f);
                }
            }
        }

        [[nodiscard]] CardInteractionResult DrawSnackbarCard(ActiveSnackbar &snackbar, ImDrawList *drawList) {
            ImGui::PushID(static_cast<int>(snackbar.event.id));

            const SeverityStyle style = GetSeverityStyle(snackbar.event.severity);
            const float cardHeight = MeasureCardHeight(snackbar);
            const ImVec2 pMin = ImGui::GetCursorScreenPos();
            const auto pMax = ImVec2(pMin.x + kCardWidth, pMin.y + cardHeight);

            // Channels: 0 = shadow + card background, 1 = icon/text/buttons
            drawList->ChannelsSplit(2);
            drawList->ChannelsSetCurrent(1);

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.20f));

            ImGui::BeginGroup();

            const ImVec2 iconCenter(pMin.x + kCardPaddingX + kIconDiameter * 0.5f, pMin.y + kCardPaddingTop + kIconDiameter * 0.5f);
            DrawCardIconAndText(drawList, snackbar, style, pMin, iconCenter);

            // Actions & close button, right-aligned, vertically centered on the icon row
            const float controlsY = pMin.y + kCardPaddingTop - 3.0f;
            float controlsX = pMax.x - kCardPaddingX;

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kButtonRounding);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));

            CardInteractionResult result;

            if (snackbar.event.dismissible && DrawDismissButton(drawList, iconCenter, controlsX)) {
                result.status = CardInteractionStatus::Dismissed;
            } else if (!snackbar.event.actions.empty()) {
                auto actionResult = DrawActionButtons(snackbar, style, controlsY, controlsX);
                if (actionResult.has_value()) {
                    result.status = CardInteractionStatus::ActionTriggered;
                    result.actionEvent = std::move(actionResult);
                }
            }

            ImGui::PopStyleVar(2);
            ImGui::EndGroup();
            ImGui::PopStyleColor(3);  // Buttons

            // Channel 0: shadow + card background, behind icon/text/buttons
            drawList->ChannelsSetCurrent(0);
            DrawCardBackgroundAndProgress(drawList, snackbar, style, pMin, pMax);
            drawList->ChannelsMerge();

            ImGui::PopID();
            return result;
        }
    }  // namespace

    /** @copydoc EditorSnackbarHost::EditorSnackbarHost */
    EditorSnackbarHost::EditorSnackbarHost(EditorDataBus &events) : events_(&events) {
        if (events_ != nullptr) {
            subscription_ = events_->Subscribe<NotificationEvent>([this](const NotificationEvent &event) {
                OnNotificationEvent(event);
            });
        }
    }

    /** @copydoc EditorSnackbarHost::~EditorSnackbarHost */
    EditorSnackbarHost::~EditorSnackbarHost() {
        Clear();
    }

    /** @copydoc EditorSnackbarHost::Clear */
    void EditorSnackbarHost::Clear() {
        activeSnackbars_.clear();
    }

    /** @copydoc EditorSnackbarHost::OnNotificationEvent */
    void EditorSnackbarHost::OnNotificationEvent(const NotificationEvent &event) {
        // NOTE: dedup only kicks in when the caller supplies a deduplicationKey.
        // If a notification appears to "double up" visually, the most likely cause
        // is the emitting call site not setting this key for repeated triggers of
        // the same logical message (e.g. re-validating on every Play attempt).
        if (!event.deduplicationKey.empty()) {
            const auto it = std::ranges::find_if(activeSnackbars_, [&event](const ActiveSnackbar &item) {
                return item.event.deduplicationKey == event.deduplicationKey;
            });
            if (it != activeSnackbars_.end()) {
                it->event.message = event.message;
                if (!event.title.empty()) {
                    it->event.title = event.title;
                }
                if (!event.actions.empty()) {
                    it->event.actions = event.actions;
                }
                it->event.severity = event.severity;
                it->event.durationSeconds = event.durationSeconds >= 0.0f ? event.durationSeconds : 8.0f;
                it->repeatCount++;
                it->elapsedSeconds = 0.0f;
                return;
            }
        }

        NotificationEvent copy = event;
        if (copy.durationSeconds < 0.0f) {
            copy.durationSeconds = 8.0f;
        }

        activeSnackbars_.push_back(ActiveSnackbar{.event = std::move(copy), .elapsedSeconds = 0.0f, .repeatCount = 1});
        EnforceQueueLimit(activeSnackbars_);
    }

    /** @copydoc EditorSnackbarHost::DismissAt */
    void EditorSnackbarHost::DismissAt(const std::size_t index) {
        if (index < activeSnackbars_.size()) {
            activeSnackbars_.erase(activeSnackbars_.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    /** @copydoc EditorSnackbarHost::Draw */
    std::optional<SnackbarActionInvokedEvent> EditorSnackbarHost::Draw(const EditorGuiContext &, const float elapsedSeconds) {
        if (activeSnackbars_.empty()) {
            return std::nullopt;
        }

        // Process timers
        UpdateTimers(activeSnackbars_, elapsedSeconds);

        if (activeSnackbars_.empty()) {
            return std::nullopt;
        }

        // Esc key dismiss top active snackbar if dismissible
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !activeSnackbars_.empty() && activeSnackbars_.front().event.dismissible) {
            DismissAt(0);
            if (activeSnackbars_.empty()) {
                return std::nullopt;
            }
        }

        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        const ImVec2 viewportSize = viewport->WorkSize;
        const ImVec2 viewportPos = viewport->WorkPos;

        const std::size_t visibleCount = std::min(activeSnackbars_.size(), kMaxVisibleSnackbars);

        // --- Pre-pass: measure exact content height for THIS frame -------------
        float totalHeight = 0.0f;
        for (std::size_t idx = 0; idx < visibleCount; ++idx) {
            totalHeight += MeasureCardHeight(activeSnackbars_[idx]);
            if (idx + 1 < visibleCount) {
                totalHeight += kCardGap;
            }
        }

        const float posX = viewportPos.x + viewportSize.x - kCardWidth - kOuterMarginRight;
        const float bottomY = viewportPos.y + viewportSize.y - kOuterMarginBottom;
        const float topY = bottomY - totalHeight;

        ImGui::SetNextWindowPos(ImVec2(posX, topY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kCardWidth + kShadowExtent, totalHeight + kShadowExtent), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                             ImGuiWindowFlags_NoBackground;

        std::optional<SnackbarActionInvokedEvent> result;

        if (ImGui::Begin("##EditorSnackbarOverlay", nullptr, windowFlags)) {
            ImDrawList *drawList = ImGui::GetWindowDrawList();

            for (std::size_t idx = 0; idx < visibleCount; ++idx) {
                const ImVec2 pMin = ImGui::GetCursorScreenPos();
                const float cardHeight = MeasureCardHeight(activeSnackbars_[idx]);
                const auto cardResult = DrawSnackbarCard(activeSnackbars_[idx], drawList);

                if (cardResult.status == CardInteractionStatus::Dismissed) {
                    DismissAt(idx);
                    break;
                }
                if (cardResult.status == CardInteractionStatus::ActionTriggered) {
                    result = cardResult.actionEvent;
                    break;
                }

                const float nextCardY = pMin.y + cardHeight + (idx + 1 < visibleCount ? kCardGap : 0.0f);
                ImGui::SetCursorScreenPos(ImVec2(pMin.x, nextCardY));
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        return result;
    }
}  // namespace Horo::Editor
