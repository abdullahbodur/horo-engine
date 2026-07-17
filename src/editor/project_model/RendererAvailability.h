#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Horo::Editor
{
/**
 * @file RendererAvailability.h
 * @brief Editor-private renderer installation, probe, selection, and activation snapshot.
 */

/** @brief Current machine-local state of one known renderer backend. */
enum class RendererAvailabilityState
{
    NotInstalled,
    Downloading,
    Staged,
    InstalledUnchecked,
    Verifying,
    Verified,
    Probing,
    HostUnsupported,
    MissingRuntime,
    AbiMismatch,
    SignatureInvalid,
    ProbeFailed,
    Quarantined,
    Available,
    Selected,
    Active,
    UpdateRequired,
    RepairRequired,
};

/** @brief One known renderer backend and its current machine-local availability. */
struct RendererBackendAvailability
{
    std::string backendId;
    std::string displayName;
    RendererAvailabilityState state = RendererAvailabilityState::NotInstalled;
    std::string diagnostic;

    /** @brief Reports whether the backend may be persisted for a newly created project. */
    [[nodiscard]] bool IsSelectable() const noexcept;
};

/** @brief Immutable renderer availability snapshot shared by editor workflows. */
class RendererAvailabilitySnapshot
{
  public:
    /**
     * @brief Constructs a snapshot from every known backend and the initialized backend identity.
     * @param entries Known backends, including unavailable ones that must remain visible in UI.
     * @param activeBackendId Backend currently driving the editor composition.
     */
    RendererAvailabilitySnapshot(std::vector<RendererBackendAvailability> entries, std::string activeBackendId);

    /** @brief Returns all known backends in stable presentation order. */
    [[nodiscard]] const std::vector<RendererBackendAvailability> &Entries() const noexcept;

    /** @brief Finds one known backend by stable ID. */
    [[nodiscard]] const RendererBackendAvailability *Find(std::string_view backendId) const noexcept;

    /** @brief Returns the backend currently driving the editor composition. */
    [[nodiscard]] std::string_view ActiveBackendId() const noexcept;

    /** @brief Returns the active backend when selectable, otherwise the first selectable backend. */
    [[nodiscard]] std::string_view DefaultSelectableBackendId() const noexcept;

  private:
    std::vector<RendererBackendAvailability> entries_;
    std::string activeBackendId_;
};
} // namespace Horo::Editor
