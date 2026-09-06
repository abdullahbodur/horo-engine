#include "Horo/Runtime/Render/RenderGraphErrors.h"

namespace Horo::Render::RenderGraphErrors {
    namespace {
        const ErrorDomainId Domain{"render.graph"};
    }

    const ErrorCodeDescriptor AllocationFailed{Domain,
                                               ErrorCode{"render.graph.allocation_failed"},
                                               ErrorSeverity::Error,
                                               "Render graph capacity reservation failed.",
                                               "Reduce graph limits or release other host memory before retrying.",
                                               true,
                                               false};
    const ErrorCodeDescriptor BuilderClosed{Domain,
                                            ErrorCode{"render.graph.builder_closed"},
                                            ErrorSeverity::Error,
                                            "Render graph builder is no longer open.",
                                            "Create a new builder after finalization, cancellation, shutdown, or move.",
                                            false,
                                            false};
    const ErrorCodeDescriptor CapacityExceeded{Domain,
                                               ErrorCode{"render.graph.capacity_exceeded"},
                                               ErrorSeverity::Error,
                                               "A declared render graph capacity is exhausted.",
                                               "Finalize this graph or create a new builder with larger admitted limits.",
                                               false,
                                               false};
    const ErrorCodeDescriptor EmptyGraph{Domain,
                                         ErrorCode{"render.graph.empty"},
                                         ErrorSeverity::Error,
                                         "A render graph with no passes cannot be finalized.",
                                         "Add at least one valid pass before finalization.",
                                         false,
                                         false};
    const ErrorCodeDescriptor IncompatibleQueue{Domain,
                                                ErrorCode{"render.graph.queue_incompatible"},
                                                ErrorSeverity::Error,
                                                "The requested queue role is incompatible with the pass kind.",
                                                "Use Graphics for graphics work and a queue capable of the compute or copy pass kind.",
                                                false,
                                                false};
    const ErrorCodeDescriptor InvalidDependency{Domain,
                                                ErrorCode{"render.graph.dependency_invalid"},
                                                ErrorSeverity::Error,
                                                "The render graph dependency is malformed or references an unknown pass.",
                                                "Use two distinct pass references issued by this open builder.",
                                                false,
                                                false};
    const ErrorCodeDescriptor InvalidLimits{Domain,
                                            ErrorCode{"render.graph.limits_invalid"},
                                            ErrorSeverity::Error,
                                            "Render graph limits are zero or exceed engine hard bounds.",
                                            "Provide non-zero capacities no greater than the documented hard maxima.",
                                            false,
                                            false};
    const ErrorCodeDescriptor InvalidPass{Domain,
                                          ErrorCode{"render.graph.pass_invalid"},
                                          ErrorSeverity::Error,
                                          "The render graph pass reference is malformed or unknown.",
                                          "Use a pass reference issued by this open builder.",
                                          false,
                                          false};
    const ErrorCodeDescriptor InvalidResource{Domain,
                                              ErrorCode{"render.graph.resource_invalid"},
                                              ErrorSeverity::Error,
                                              "The render graph resource identity is malformed or unknown.",
                                              "Use a resource identity issued by this open builder.",
                                              false,
                                              false};
    const ErrorCodeDescriptor InvalidUsage{Domain,
                                           ErrorCode{"render.graph.usage_invalid"},
                                           ErrorSeverity::Error,
                                           "The render graph resource use is malformed or semantically incompatible.",
                                           "Use compatible pass, resource, access, and usage values issued by this builder.",
                                           false,
                                           false};
    const ErrorCodeDescriptor OwnerExhausted{Domain,
                                             ErrorCode{"render.graph.owner_exhausted"},
                                             ErrorSeverity::Critical,
                                             "Render graph owner identities are exhausted.",
                                             "Restart the process rather than reusing a graph owner identity.",
                                             false,
                                             false};
    const ErrorCodeDescriptor UnsupportedPassKind{Domain,
                                                  ErrorCode{"render.graph.pass_kind_unsupported"},
                                                  ErrorSeverity::Error,
                                                  "The render pass kind is unsupported.",
                                                  "Request a declared Graphics, Compute, or Copy pass without fallback.",
                                                  false,
                                                  false};
    const ErrorCodeDescriptor UnsupportedDependencyKind{Domain,
                                                        ErrorCode{"render.graph.dependency_kind_unsupported"},
                                                        ErrorSeverity::Error,
                                                        "The render graph dependency kind is unsupported.",
                                                        "Request a declared execution-order, resource-hazard, or external-synchronization "
                                                        "dependency without fallback.",
                                                        false,
                                                        false};
    const ErrorCodeDescriptor UnsupportedQueueRole{Domain,
                                                   ErrorCode{"render.graph.queue_role_unsupported"},
                                                   ErrorSeverity::Error,
                                                   "The render queue role is unsupported.",
                                                   "Request a declared Graphics, Compute, or Transfer role without fallback.",
                                                   false,
                                                   false};
    const ErrorCodeDescriptor UnsupportedResourceKind{Domain,
                                                      ErrorCode{"render.graph.resource_kind_unsupported"},
                                                      ErrorSeverity::Error,
                                                      "The render graph resource kind is unsupported.",
                                                      "Request a declared Buffer or Texture resource without fallback.",
                                                      false,
                                                      false};
    const ErrorCodeDescriptor UnsupportedUsage{Domain,
                                               ErrorCode{"render.graph.usage_unsupported"},
                                               ErrorSeverity::Error,
                                               "The render graph access or usage kind is unsupported.",
                                               "Use one of the declared access and semantic usage values without fallback.",
                                               false,
                                               false};
    const ErrorCodeDescriptor WrongOwner{Domain,
                                         ErrorCode{"render.graph.wrong_owner"},
                                         ErrorSeverity::Error,
                                         "The render graph reference belongs to another builder.",
                                         "Use only pass and resource references issued by the receiving builder.",
                                         false,
                                         false};
    const ErrorCodeDescriptor WrongThread{Domain,
                                          ErrorCode{"render.graph.wrong_thread"},
                                          ErrorSeverity::Error,
                                          "Render graph authoring was attempted from a non-owner thread.",
                                          "Author and finalize the graph on the thread that created its builder.",
                                          false,
                                          false};
}  // namespace Horo::Render::RenderGraphErrors
