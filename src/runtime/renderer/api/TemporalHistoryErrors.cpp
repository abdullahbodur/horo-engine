#include "Horo/Runtime/Render/TemporalHistoryErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::TemporalHistoryErrors {
    namespace {
        const ErrorDomainId Domain{"render.temporal_history"};
    }

    const ErrorCodeDescriptor AllocationFailed =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.allocation_failed", ErrorSeverity::Error,
                                    "Temporal history storage allocation failed.",
                                    "Reduce admitted history limits or release host memory before retrying.", true);
    const ErrorCodeDescriptor CapacityExceeded =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.capacity_exceeded", ErrorSeverity::Error,
                                    "The temporal history store is full.",
                                    "Retire an unused history or create a store with a larger admitted capacity.");
    const ErrorCodeDescriptor FrameAlreadyPending =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.frame_pending", ErrorSeverity::Error,
                                    "A temporal history already has a pending frame.",
                                    "Publish or abandon the pending frame before beginning, resetting, or retiring this history.");
    const ErrorCodeDescriptor InvalidDescriptor =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.descriptor_invalid", ErrorSeverity::Error,
                                    "The temporal history descriptor is incomplete.",
                                    "Provide valid view/provider/mode identities, extents, and every compatibility generation.");
    const ErrorCodeDescriptor InvalidFrame =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.frame_invalid", ErrorSeverity::Error,
                                    "The temporal history frame is malformed or stale.",
                                    "Use the exact pending frame snapshot returned by BeginFrame.");
    const ErrorCodeDescriptor InvalidHandle =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.handle_invalid", ErrorSeverity::Error,
                                    "The temporal history handle is malformed or retired.",
                                    "Use a live generation-safe handle issued by this store.");
    const ErrorCodeDescriptor InvalidLimits =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.limits_invalid", ErrorSeverity::Error,
                                    "Temporal history limits are zero or exceed hard bounds.",
                                    "Provide finite non-zero capacities within the documented maxima.");
    const ErrorCodeDescriptor InvalidResetCause =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.reset_cause_invalid", ErrorSeverity::Error,
                                    "The temporal history reset cause is unsupported.",
                                    "Use one of the declared backend-neutral reset causes without fallback.");
    const ErrorCodeDescriptor OwnerExhausted =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.owner_exhausted", ErrorSeverity::Critical,
                                    "Temporal history owner identities are exhausted.",
                                    "Restart the process rather than reusing an owner identity.");
    const ErrorCodeDescriptor ResetRequired =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.reset_required", ErrorSeverity::Error,
                                    "The temporal history frame sequence is incompatible.",
                                    "Reset the history with an explicit cause and exact replacement compatibility/resources.");
    const ErrorCodeDescriptor StoreStopped =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.store_stopped", ErrorSeverity::Error,
                                    "The temporal history store has stopped.", "Create a new store for a new renderer lifetime.");
    const ErrorCodeDescriptor WrongOwner =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.wrong_owner", ErrorSeverity::Error,
                                    "The temporal history belongs to another store.", "Route the handle back to the store that issued it.");
    const ErrorCodeDescriptor WrongThread =
        Detail::MakeErrorDescriptor(Domain, "render.temporal_history.wrong_thread", ErrorSeverity::Error,
                                    "Temporal history mutation used a non-owner thread.",
                                    "Perform history lifecycle operations at the renderer owner-thread safe point.");
}  // namespace Horo::Render::TemporalHistoryErrors
