#include "FoundationErrors.h"

namespace Horo
{
namespace
{
const ErrorDomainId ConfigurationDomain{"horo.configuration"};
const ErrorDomainId JobDomain{"horo.foundation.jobs"};
const ErrorDomainId MathDomain{"horo.foundation.math"};

[[nodiscard]] ErrorCodeDescriptor Describe(const ErrorDomainId &domain, const char *code, const char *summary,
                                           const char *remediation, const bool retryable = false,
                                           const bool userActionable = false)
{
    return ErrorCodeDescriptor{.domain = domain,
                               .code = ErrorCode{code},
                               .defaultSeverity = ErrorSeverity::Error,
                               .summary = summary,
                               .remediationHint = remediation,
                               .retryable = retryable,
                               .userActionable = userActionable};
}
} // namespace

namespace ConfigurationErrors
{
const ErrorCodeDescriptor SchemaInvalid = Describe(ConfigurationDomain, "configuration.schema_invalid",
                                                   "Configuration schema is invalid.",
                                                   "Correct the schema descriptor before registration.");
const ErrorCodeDescriptor SchemaSealed = Describe(ConfigurationDomain, "configuration.schema_sealed",
                                                  "Configuration schema is already sealed.",
                                                  "Register settings before sealing the schema.");
const ErrorCodeDescriptor DraftStale = Describe(ConfigurationDomain, "configuration.draft_stale",
                                                "Configuration draft is stale.",
                                                "Refresh the draft from the active configuration and retry.", true);
const ErrorCodeDescriptor ValueInvalid = Describe(ConfigurationDomain, "configuration.value_invalid",
                                                  "Configuration value is invalid.",
                                                  "Provide a value matching the registered setting type.", false, true);
const ErrorCodeDescriptor JsonParseError = Describe(ConfigurationDomain, "configuration.json_parse_error",
                                                    "Configuration JSON could not be parsed.",
                                                    "Repair or regenerate the configuration file.", false, true);
const ErrorCodeDescriptor FileNotFound = Describe(ConfigurationDomain, "configuration.file_not_found",
                                                  "Configuration file was not found.",
                                                  "Verify the configuration path.", false, true);
const ErrorCodeDescriptor FileWriteError = Describe(ConfigurationDomain, "configuration.file_write_error",
                                                    "Configuration file could not be written.",
                                                    "Verify destination permissions and available storage.", true, true);
} // namespace ConfigurationErrors

namespace JobErrors
{
const ErrorCodeDescriptor Cancelled = Describe(JobDomain, "job.cancelled", "Job was cancelled.",
                                               "Retry only if the owning operation is still active.", true);
const ErrorCodeDescriptor Failed = Describe(JobDomain, "job.failed", "Job execution failed.",
                                            "Inspect the job error and diagnostics before retrying.");
const ErrorCodeDescriptor InvalidHandle = Describe(JobDomain, "job.invalid_handle", "Job handle is invalid.",
                                                   "Use a handle returned by the active job system.");
const ErrorCodeDescriptor NotFound = Describe(JobDomain, "job.not_found", "Job was not found.",
                                             "Verify that the job belongs to the active job system.");
const ErrorCodeDescriptor QueueFull = Describe(JobDomain, "job.queue_full", "Job queue is full.",
                                              "Retry after queued work completes.", true);
const ErrorCodeDescriptor Shutdown = Describe(JobDomain, "job.shutdown", "Job system is shutting down.",
                                             "Do not submit new work after shutdown begins.");
} // namespace JobErrors

namespace Math::Errors
{
const ErrorCodeDescriptor InvalidAffineMatrix = Describe(MathDomain, "math.invalid_affine_matrix",
                                                         "Matrix is not a valid affine transform.",
                                                         "Provide a finite affine matrix.");
const ErrorCodeDescriptor InvalidBounds = Describe(MathDomain, "math.invalid_bounds", "Bounds are invalid.",
                                                   "Provide finite ordered bounds.");
const ErrorCodeDescriptor InvalidHomogeneousPoint = Describe(MathDomain, "math.invalid_homogeneous_point",
                                                             "Homogeneous point is invalid.",
                                                             "Use a finite point with a valid homogeneous divisor.");
const ErrorCodeDescriptor InvalidPlane = Describe(MathDomain, "math.invalid_plane", "Plane is invalid.",
                                                  "Provide a finite plane with a non-zero normal.");
const ErrorCodeDescriptor InvalidProjection = Describe(MathDomain, "math.invalid_projection",
                                                       "Projection parameters are invalid.",
                                                       "Provide finite projection parameters with valid ranges.");
const ErrorCodeDescriptor InvalidRay = Describe(MathDomain, "math.invalid_ray", "Ray is invalid.",
                                                "Provide a finite normalized direction and ordered distances.");
const ErrorCodeDescriptor InvalidView = Describe(MathDomain, "math.invalid_view", "View parameters are invalid.",
                                                 "Provide distinct finite eye and target vectors with a valid up vector.");
const ErrorCodeDescriptor NonFiniteInput = Describe(MathDomain, "math.non_finite_input",
                                                    "Math input contains a non-finite value.",
                                                    "Remove NaN or infinity values before the operation.");
const ErrorCodeDescriptor SingularMatrix = Describe(MathDomain, "math.singular_matrix", "Matrix is singular.",
                                                    "Provide an invertible matrix.");
const ErrorCodeDescriptor ZeroLength = Describe(MathDomain, "math.zero_length", "Vector length is zero.",
                                                "Provide a vector with non-zero length.");
} // namespace Math::Errors
} // namespace Horo
