#include "Horo/Runtime/Render/RenderGraphLifetimeErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::RenderGraphLifetimeErrors {
    namespace {
        const ErrorDomainId Domain{"render.graph.lifetime"};
    }  // namespace

    const ErrorCodeDescriptor AllocationFailed =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.allocation_failed", ErrorSeverity::Error,
                                    "Render graph lifetime planning storage allocation failed.",
                                    "Reduce graph limits or release other host memory before retrying.", true);
    const ErrorCodeDescriptor CapacityExceeded =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.capacity_exceeded", ErrorSeverity::Error,
                                    "The admitted render graph lifetime output capacity was exceeded.",
                                    "Increase the bounded lifetime-plan capacity or reduce transient alias reuse in this graph.");
    const ErrorCodeDescriptor DescriptorInvalid =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.descriptor_invalid", ErrorSeverity::Error,
                                    "A transient storage descriptor is invalid or does not match its graph resource kind.",
                                    "Provide a valid buffer descriptor for a buffer or texture descriptor for a texture.");
    const ErrorCodeDescriptor DuplicateRequirement =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.requirement_duplicate", ErrorSeverity::Error,
                                    "A transient graph resource has more than one allocation requirement.",
                                    "Declare exactly one storage requirement for each transient graph resource.");
    const ErrorCodeDescriptor InvalidGraph =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.graph_invalid", ErrorSeverity::Error,
                                    "The finalized graph is moved-from or violates resource identity bounds.",
                                    "Compile an intact finalized graph.");
    const ErrorCodeDescriptor InvalidLimits =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.limits_invalid", ErrorSeverity::Error,
                                    "Render graph lifetime limits exceed their engine hard bounds.",
                                    "Use output capacities no greater than the documented hard maxima.");
    const ErrorCodeDescriptor InvalidSchedule =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.schedule_invalid", ErrorSeverity::Error,
                                    "The render graph schedule is moved-from, duplicated, or references an unknown pass.",
                                    "Use the intact schedule compiled from the supplied graph.");
    const ErrorCodeDescriptor MissingRequirement =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.requirement_missing", ErrorSeverity::Error,
                                    "A transient graph resource has no allocation requirement.",
                                    "Declare one backend-neutral storage requirement for every transient resource.");
    const ErrorCodeDescriptor OwnerMismatch =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.owner_mismatch", ErrorSeverity::Error,
                                    "Lifetime planning inputs belong to different render graph owners.",
                                    "Use a graph, schedule, and resource requirements issued for the same graph owner.");
    const ErrorCodeDescriptor UnexpectedRequirement =
        Detail::MakeErrorDescriptor(Domain, "render.graph.lifetime.requirement_unexpected", ErrorSeverity::Error,
                                    "An allocation requirement references an imported or unknown graph resource.",
                                    "Declare allocation requirements only for transient resources in the supplied graph.");
}  // namespace Horo::Render::RenderGraphLifetimeErrors
