#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{

/**
 * @file EditorStatusBarModel.h
 * @brief Typed, bounded model and layout planning contract for editor status-bar contributions.
 */

namespace EditorStatusItemLimits
{
inline constexpr std::size_t MaxItems = 64;
inline constexpr std::size_t MaxIdBytes = 128;
inline constexpr std::size_t MaxIconIdBytes = 128;
inline constexpr std::size_t MaxLabelBytes = 96;
inline constexpr std::size_t MaxValueBytes = 64;
inline constexpr std::size_t MaxActionIdBytes = 128;
inline constexpr float MinWidth = 32.0F;
inline constexpr float MaxWidth = 240.0F;
} // namespace EditorStatusItemLimits

/** @brief Horizontal group where a status item is presented. */
enum class EditorStatusBarAlignment : std::uint8_t
{
    Left,
    Right,
};

/** @brief Host-controlled visibility policy for a status item. */
enum class EditorStatusItemVisibility : std::uint8_t
{
    Always,
    OnlyWhenPanelActive,
};

/** @brief Semantic tone rendered through the active editor design tokens. */
enum class EditorStatusItemTone : std::uint8_t
{
    Neutral,
    Accent,
    Success,
    Warning,
    Error,
};

/** @brief Host-rendered visual treatment for a status contribution. */
enum class EditorStatusItemPresentation : std::uint8_t
{
    KeyValue,
    Pill,
    Plain,
};

/** @brief Stable validation/result code returned by status-item registry operations. */
enum class EditorStatusItemError : std::uint8_t
{
    None,
    InvalidId,
    DuplicateId,
    MissingOwnerPanel,
    MissingAction,
    InvalidWidth,
    DescriptorTooLong,
    ContentTooLong,
    RegistryFull,
    UnknownItem,
};

/** @brief Result of a status-item registry mutation. */
struct EditorStatusItemResult
{
    EditorStatusItemError error = EditorStatusItemError::None;

    /** @brief Returns true when the operation completed successfully. */
    operator bool() const noexcept
    {
        return error == EditorStatusItemError::None;
    }
};

/** @brief Immutable registration metadata owned and validated by the host registry. */
struct EditorStatusItemDescriptor
{
    std::string id;
    std::string localizationNamespace = "editor";
    std::string labelKey;
    EditorStatusBarAlignment alignment = EditorStatusBarAlignment::Left;
    EditorStatusItemVisibility visibility = EditorStatusItemVisibility::Always;
    std::string ownerPanelId;
    int priority = 0;
    std::uint16_t order = 0;
    float maxWidth = 180.0F;
    EditorStatusItemPresentation presentation = EditorStatusItemPresentation::KeyValue;
    bool interactive = false;
    std::string actionId;
};

/** @brief Bounded presentation snapshot updated by a built-in or extension provider. */
struct EditorStatusItemContent
{
    std::string iconResourceId;
    std::string label;
    std::string value;
    EditorStatusItemTone tone = EditorStatusItemTone::Neutral;
    bool available = true;
};

/** @brief One accepted status-item descriptor and its latest presentation snapshot. */
struct EditorStatusItem
{
    EditorStatusItemDescriptor descriptor;
    EditorStatusItemContent content;
};

/** @brief Per-frame host context used to resolve declarative visibility policies. */
struct EditorStatusBarContext
{
    std::span<const std::string_view> activePanelIds;
};

/** @brief Status item paired with renderer-measured preferred width. */
struct EditorStatusMeasuredItem
{
    const EditorStatusItem *item = nullptr;
    float measuredWidth = 0.0F;
};

/** @brief Width-budgeted item selection and omitted contribution count. */
struct EditorStatusBarLayout
{
    std::vector<const EditorStatusItem *> items;
    std::size_t hiddenCount = 0;
};

/** @brief Process-level event published after an enabled status contribution is invoked. */
struct EditorStatusItemInvokedEvent
{
    static constexpr auto HoroEventTypeName = "Horo.Editor.EditorStatusItemInvokedEvent";
    std::string itemId;
    std::string actionId;
};

/** @brief Host-owned typed registry for built-in, module, and plugin status contributions. */
class EditorStatusItemRegistry
{
  public:
    /**
     * @brief Registers one contribution after validating descriptor and initial content.
     * @param descriptor Stable placement, visibility, and action metadata.
     * @param content Initial bounded presentation snapshot.
     * @return Success or a typed validation/duplicate/capacity error.
     */
    [[nodiscard]] EditorStatusItemResult Register(EditorStatusItemDescriptor descriptor,
                                                  EditorStatusItemContent content);

    /**
     * @brief Atomically replaces one contribution's presentation snapshot.
     * @param id Stable registered contribution ID.
     * @param content New bounded snapshot.
     * @return Success or a typed unknown-item/content-validation error.
     */
    [[nodiscard]] EditorStatusItemResult Update(std::string_view id, EditorStatusItemContent content);

    /**
     * @brief Removes one registered contribution.
     * @param id Stable registered contribution ID.
     * @return Success or UnknownItem when no contribution exists.
     */
    [[nodiscard]] EditorStatusItemResult Unregister(std::string_view id);

    /**
     * @brief Returns one contribution by stable ID, or null when absent.
     * @return Borrowed pointer stable across unrelated registry mutations; invalid after this item is unregistered or
     * the registry is destroyed.
     */
    [[nodiscard]] const EditorStatusItem *Find(std::string_view id) const noexcept;

    /**
     * @brief Resolves currently available contributions in deterministic presentation order.
     * @param context Active panel context supplied by the editor shell.
     * @return Left items followed by right items, ordered by explicit order and stable ID.
     */
    [[nodiscard]] std::vector<const EditorStatusItem *> VisibleItems(const EditorStatusBarContext &context) const;

    /**
     * @brief Appends visible contributions into reusable caller storage without requiring a fresh allocation.
     * @param context Active panel context supplied by the editor shell.
     * @param output Reusable vector cleared and populated by this call. Borrowed item pointers remain stable across
     * unrelated mutations and expire when their item is unregistered or the registry is destroyed.
     */
    void CollectVisibleItems(const EditorStatusBarContext &context,
                             std::vector<const EditorStatusItem *> &output) const;

    /** @brief Returns the number of accepted contributions. */
    [[nodiscard]] std::size_t Size() const noexcept;

  private:
    // Pointees remain stable across unrelated register/unregister operations.
    std::vector<std::unique_ptr<EditorStatusItem>> items_;
};

/**
 * @brief Selects contributions that fit a bounded status-bar width.
 * @param measured Visible items paired with renderer-measured preferred widths.
 * @param availableWidth Total width available to contributions and overflow indicator.
 * @param itemGap Horizontal gap between accepted items.
 * @param overflowWidth Width reserved when one or more contributions are omitted.
 * @param maxVisibleItems Hard cap on visible contributions.
 * @return Deterministically ordered accepted items and hidden contribution count.
 */
[[nodiscard]] EditorStatusBarLayout PlanEditorStatusBarLayout(std::span<const EditorStatusMeasuredItem> measured,
                                                              float availableWidth, float itemGap, float overflowWidth,
                                                              std::size_t maxVisibleItems);

/**
 * @brief Plans into reusable caller storage for allocation-free steady-state rendering.
 * @param measured Visible items paired with renderer-measured preferred widths.
 * @param availableWidth Total width available to contributions and overflow indicator.
 * @param itemGap Horizontal gap between accepted items.
 * @param overflowWidth Width reserved when one or more contributions are omitted.
 * @param maxVisibleItems Hard cap on visible contributions.
 * @param rankedScratch Reusable ranking storage, cleared by the function.
 * @param output Reusable result storage, cleared and populated by the function.
 */
void PlanEditorStatusBarLayoutInto(std::span<const EditorStatusMeasuredItem> measured, float availableWidth,
                                   float itemGap, float overflowWidth, std::size_t maxVisibleItems,
                                   std::vector<EditorStatusMeasuredItem> &rankedScratch, EditorStatusBarLayout &output);

} // namespace Horo::Editor
