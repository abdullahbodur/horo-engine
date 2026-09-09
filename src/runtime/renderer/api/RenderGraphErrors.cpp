#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include "RenderErrorDescriptor.h"

namespace Horo::Render::RenderGraphErrors {
    namespace {
        const ErrorDomainId Domain{"render.graph"};
    }  // namespace

    const ErrorCodeDescriptor AllocationFailed =
        Detail::MakeErrorDescriptor(Domain, "render.graph.allocation_failed", ErrorSeverity::Error,
                                    "Render graph storage allocation failed.",
                                    "Reduce graph limits or release other host memory before retrying.", true);
    const ErrorCodeDescriptor BuilderClosed =
        Detail::MakeErrorDescriptor(Domain, "render.graph.builder_closed", ErrorSeverity::Error, "Render graph builder is no longer open.",
                                    "Create a new builder after finalization, cancellation, shutdown, or move.");
    const ErrorCodeDescriptor CapacityExceeded =
        Detail::MakeErrorDescriptor(Domain, "render.graph.capacity_exceeded", ErrorSeverity::Error,
                                    "A declared render graph capacity is exhausted.",
                                    "Finalize this graph or create a new builder with larger admitted limits.");
    const ErrorCodeDescriptor DependencyCycle =
        Detail::MakeErrorDescriptor(Domain, "render.graph.dependency_cycle", ErrorSeverity::Error,
                                    "The render graph contains a directed dependency cycle.",
                                    "Remove or redirect a dependency so every pass has an acyclic execution order.");
    const ErrorCodeDescriptor EmptyGraph = Detail::MakeErrorDescriptor(Domain, "render.graph.empty", ErrorSeverity::Error,
                                                                       "A render graph with no passes cannot be finalized.",
                                                                       "Add at least one valid pass before finalization.");
    const ErrorCodeDescriptor IncompatibleQueue =
        Detail::MakeErrorDescriptor(Domain, "render.graph.queue_incompatible", ErrorSeverity::Error,
                                    "The requested queue role is incompatible with the pass kind.",
                                    "Use Graphics for graphics work and a queue capable of the compute or copy pass kind.");
    const ErrorCodeDescriptor InvalidDependency =
        Detail::MakeErrorDescriptor(Domain, "render.graph.dependency_invalid", ErrorSeverity::Error,
                                    "The render graph dependency is malformed or references an unknown pass.",
                                    "Use two distinct pass references issued by this open builder.");
    const ErrorCodeDescriptor InvalidExport = Detail::MakeErrorDescriptor(Domain, "render.graph.export_invalid", ErrorSeverity::Error,
                                                                          "The render graph export is duplicated or malformed.",
                                                                          "Export each valid graph-local resource at most once.");
    const ErrorCodeDescriptor InvalidGraph =
        Detail::MakeErrorDescriptor(Domain, "render.graph.invalid", ErrorSeverity::Error,
                                    "The render graph is moved-from or violates its finalized structural invariants.",
                                    "Compile only the intact finalized graph returned by its builder.");
    const ErrorCodeDescriptor InvalidImport = Detail::
        MakeErrorDescriptor(Domain, "render.graph.import_invalid", ErrorSeverity::Error,
                            "The render graph import has an invalid resident binding or lifetime class.",
                            "Import a valid generation-safe Horo buffer or texture handle as external, persistent, or history data.");
    const ErrorCodeDescriptor InvalidLimits =
        Detail::MakeErrorDescriptor(Domain, "render.graph.limits_invalid", ErrorSeverity::Error,
                                    "Render graph limits are zero or exceed engine hard bounds.",
                                    "Provide non-zero capacities no greater than the documented hard maxima.");
    const ErrorCodeDescriptor InvalidPass = Detail::MakeErrorDescriptor(Domain, "render.graph.pass_invalid", ErrorSeverity::Error,
                                                                        "The render graph pass reference is malformed or unknown.",
                                                                        "Use a pass reference issued by this open builder.");
    const ErrorCodeDescriptor InvalidResource = Detail::MakeErrorDescriptor(Domain, "render.graph.resource_invalid", ErrorSeverity::Error,
                                                                            "The render graph resource identity is malformed or unknown.",
                                                                            "Use a resource identity issued by this open builder.");
    const ErrorCodeDescriptor InvalidUsage =
        Detail::MakeErrorDescriptor(Domain, "render.graph.usage_invalid", ErrorSeverity::Error,
                                    "The render graph resource use is malformed or semantically incompatible.",
                                    "Use compatible pass, resource, access, and usage values issued by this builder.");
    const ErrorCodeDescriptor OwnerExhausted =
        Detail::MakeErrorDescriptor(Domain, "render.graph.owner_exhausted", ErrorSeverity::Critical,
                                    "Render graph owner identities are exhausted.",
                                    "Restart the process rather than reusing a graph owner identity.");
    const ErrorCodeDescriptor ReadBeforeWrite =
        Detail::MakeErrorDescriptor(Domain, "render.graph.read_before_write", ErrorSeverity::Error,
                                    "A transient graph resource is read before any ordered pass writes it.",
                                    "Add an ordered producer, or import resident data for later exact state validation.");
    const ErrorCodeDescriptor UnsupportedDependencyKind =
        Detail::MakeErrorDescriptor(Domain, "render.graph.dependency_kind_unsupported", ErrorSeverity::Error,
                                    "The render graph dependency kind is unsupported.",
                                    "Request a declared execution-order, resource-hazard, or external-synchronization dependency without "
                                    "fallback.");
    const ErrorCodeDescriptor UnsupportedPassKind =
        Detail::MakeErrorDescriptor(Domain, "render.graph.pass_kind_unsupported", ErrorSeverity::Error,
                                    "The render pass kind is unsupported.",
                                    "Request a declared Graphics, Compute, or Copy pass without fallback.");
    const ErrorCodeDescriptor UnsupportedPassCullPolicy =
        Detail::MakeErrorDescriptor(Domain, "render.graph.pass_cull_policy_unsupported", ErrorSeverity::Error,
                                    "The render pass culling policy is unsupported.",
                                    "Use ConservativeKeep or explicitly opt into AllowCullIfOutputsUnused.");
    const ErrorCodeDescriptor UnsupportedQueueRole =
        Detail::MakeErrorDescriptor(Domain, "render.graph.queue_role_unsupported", ErrorSeverity::Error,
                                    "The render queue role is unsupported.",
                                    "Request a declared Graphics, Compute, or Transfer role without fallback.");
    const ErrorCodeDescriptor UnsupportedResourceKind =
        Detail::MakeErrorDescriptor(Domain, "render.graph.resource_kind_unsupported", ErrorSeverity::Error,
                                    "The render graph resource kind is unsupported.",
                                    "Request a declared Buffer or Texture resource without fallback.");
    const ErrorCodeDescriptor UnsupportedResourceClass =
        Detail::MakeErrorDescriptor(Domain, "render.graph.resource_class_unsupported", ErrorSeverity::Error,
                                    "The render graph resource lifetime class is unsupported.",
                                    "Use External, Persistent, Transient, or History without fallback.");
    const ErrorCodeDescriptor UnsupportedUsage =
        Detail::MakeErrorDescriptor(Domain, "render.graph.usage_unsupported", ErrorSeverity::Error,
                                    "The render graph access or usage kind is unsupported.",
                                    "Use one of the declared access and semantic usage values without fallback.");
    const ErrorCodeDescriptor WrongOwner =
        Detail::MakeErrorDescriptor(Domain, "render.graph.wrong_owner", ErrorSeverity::Error,
                                    "The render graph reference belongs to another builder.",
                                    "Use only pass and resource references issued by the receiving builder.");
    const ErrorCodeDescriptor WrongThread =
        Detail::MakeErrorDescriptor(Domain, "render.graph.wrong_thread", ErrorSeverity::Error,
                                    "Render graph authoring was attempted from a non-owner thread.",
                                    "Author and finalize the graph on the thread that created its builder.");
}  // namespace Horo::Render::RenderGraphErrors
