#include "Horo/Runtime/Render/RenderGraphErrors.h"

#include <string>
#include <string_view>
#include <utility>

namespace Horo::Render::RenderGraphErrors {
    namespace {
        const ErrorDomainId Domain{"render.graph"};

        /** @brief Builds one immutable descriptor in the render-graph error domain. */
        ErrorCodeDescriptor Descriptor(std::string code, const ErrorSeverity severity, const std::string_view summary,
                                       const std::string_view remediation, const bool retryable = false) {
            return {Domain, ErrorCode{std::move(code)}, severity, summary, remediation, retryable, false};
        }
    }  // namespace

    const ErrorCodeDescriptor AllocationFailed =
        Descriptor("render.graph.allocation_failed", ErrorSeverity::Error, "Render graph capacity reservation failed.",
                   "Reduce graph limits or release other host memory before retrying.", true);
    const ErrorCodeDescriptor BuilderClosed =
        Descriptor("render.graph.builder_closed", ErrorSeverity::Error, "Render graph builder is no longer open.",
                   "Create a new builder after finalization, cancellation, shutdown, or move.");
    const ErrorCodeDescriptor CapacityExceeded =
        Descriptor("render.graph.capacity_exceeded", ErrorSeverity::Error, "A declared render graph capacity is exhausted.",
                   "Finalize this graph or create a new builder with larger admitted limits.");
    const ErrorCodeDescriptor EmptyGraph =
        Descriptor("render.graph.empty", ErrorSeverity::Error, "A render graph with no passes cannot be finalized.",
                   "Add at least one valid pass before finalization.");
    const ErrorCodeDescriptor IncompatibleQueue =
        Descriptor("render.graph.queue_incompatible", ErrorSeverity::Error, "The requested queue role is incompatible with the pass kind.",
                   "Use Graphics for graphics work and a queue capable of the compute or copy pass kind.");
    const ErrorCodeDescriptor InvalidDependency = Descriptor("render.graph.dependency_invalid", ErrorSeverity::Error,
                                                             "The render graph dependency is malformed or references an unknown pass.",
                                                             "Use two distinct pass references issued by this open builder.");
    const ErrorCodeDescriptor InvalidLimits =
        Descriptor("render.graph.limits_invalid", ErrorSeverity::Error, "Render graph limits are zero or exceed engine hard bounds.",
                   "Provide non-zero capacities no greater than the documented hard maxima.");
    const ErrorCodeDescriptor InvalidPass =
        Descriptor("render.graph.pass_invalid", ErrorSeverity::Error, "The render graph pass reference is malformed or unknown.",
                   "Use a pass reference issued by this open builder.");
    const ErrorCodeDescriptor InvalidResource =
        Descriptor("render.graph.resource_invalid", ErrorSeverity::Error, "The render graph resource identity is malformed or unknown.",
                   "Use a resource identity issued by this open builder.");
    const ErrorCodeDescriptor InvalidUsage = Descriptor("render.graph.usage_invalid", ErrorSeverity::Error,
                                                        "The render graph resource use is malformed or semantically incompatible.",
                                                        "Use compatible pass, resource, access, and usage values issued by this builder.");
    const ErrorCodeDescriptor OwnerExhausted =
        Descriptor("render.graph.owner_exhausted", ErrorSeverity::Critical, "Render graph owner identities are exhausted.",
                   "Restart the process rather than reusing a graph owner identity.");
    const ErrorCodeDescriptor UnsupportedDependencyKind =
        Descriptor("render.graph.dependency_kind_unsupported", ErrorSeverity::Error, "The render graph dependency kind is unsupported.",
                   "Request a declared execution-order, resource-hazard, or external-synchronization dependency without fallback.");
    const ErrorCodeDescriptor UnsupportedPassKind =
        Descriptor("render.graph.pass_kind_unsupported", ErrorSeverity::Error, "The render pass kind is unsupported.",
                   "Request a declared Graphics, Compute, or Copy pass without fallback.");
    const ErrorCodeDescriptor UnsupportedQueueRole =
        Descriptor("render.graph.queue_role_unsupported", ErrorSeverity::Error, "The render queue role is unsupported.",
                   "Request a declared Graphics, Compute, or Transfer role without fallback.");
    const ErrorCodeDescriptor UnsupportedResourceKind =
        Descriptor("render.graph.resource_kind_unsupported", ErrorSeverity::Error, "The render graph resource kind is unsupported.",
                   "Request a declared Buffer or Texture resource without fallback.");
    const ErrorCodeDescriptor UnsupportedUsage =
        Descriptor("render.graph.usage_unsupported", ErrorSeverity::Error, "The render graph access or usage kind is unsupported.",
                   "Use one of the declared access and semantic usage values without fallback.");
    const ErrorCodeDescriptor WrongOwner =
        Descriptor("render.graph.wrong_owner", ErrorSeverity::Error, "The render graph reference belongs to another builder.",
                   "Use only pass and resource references issued by the receiving builder.");
    const ErrorCodeDescriptor WrongThread =
        Descriptor("render.graph.wrong_thread", ErrorSeverity::Error, "Render graph authoring was attempted from a non-owner thread.",
                   "Author and finalize the graph on the thread that created its builder.");
}  // namespace Horo::Render::RenderGraphErrors
