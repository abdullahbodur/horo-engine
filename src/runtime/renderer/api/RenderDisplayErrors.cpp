#include "Horo/Runtime/Render/RenderDisplayErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::RenderDisplayErrors {
    namespace {
        const ErrorDomainId Domain{"render.display"};
    }  // namespace

    const ErrorCodeDescriptor InvalidSnapshot =
        Detail::MakeErrorDescriptor(Domain, "render.display.snapshot_invalid", ErrorSeverity::Error,
                                    "Display facts are malformed, unbounded, duplicated, or unordered.",
                                    "Reject the platform snapshot and retain the last committed display revision.");
    const ErrorCodeDescriptor UnsupportedFact =
        Detail::MakeErrorDescriptor(Domain, "render.display.fact_unsupported", ErrorSeverity::Error,
                                    "Display facts contain an unknown value or unsupported color/HDR combination.",
                                    "Update the platform translation or omit the unavailable fact explicitly.");
    const ErrorCodeDescriptor StaleRevision =
        Detail::MakeErrorDescriptor(Domain, "render.display.revision_stale", ErrorSeverity::Warning,
                                    "The display snapshot revision is older than the committed revision.",
                                    "Discard the stale platform observation and publish a newer complete snapshot.", true);
    const ErrorCodeDescriptor RevisionUnchanged =
        Detail::MakeErrorDescriptor(Domain, "render.display.revision_unchanged", ErrorSeverity::Warning,
                                    "The display snapshot revision did not advance.",
                                    "Coalesce equal-revision observations instead of republishing them.", true);
    const ErrorCodeDescriptor ThreadAffinityViolation =
        Detail::MakeErrorDescriptor(Domain, "render.display.thread_affinity_violation", ErrorSeverity::Error,
                                    "Display snapshot publication was attempted from a non-owner thread.",
                                    "Transfer the immutable facts to the platform owner-thread safe point.");
    const ErrorCodeDescriptor FeedStopped = Detail::MakeErrorDescriptor(Domain, "render.display.feed_stopped", ErrorSeverity::Error,
                                                                        "Display snapshot publication admission is closed.",
                                                                        "Create a new feed after the platform owner is initialized again.");
    const ErrorCodeDescriptor SnapshotUnavailable =
        Detail::MakeErrorDescriptor(Domain, "render.display.snapshot_unavailable", ErrorSeverity::Info,
                                    "No display snapshot has been committed.",
                                    "Publish one validated platform snapshot before requesting display facts.", true);
}  // namespace Horo::Render::RenderDisplayErrors
