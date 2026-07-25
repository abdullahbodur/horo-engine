#include "FoundationErrors.h"

namespace Horo
{
    namespace
    {
        const ErrorDomainId ConfigurationDomain{"horo.configuration"};
        const ErrorDomainId HashingDomain{"horo.foundation.hashing"};
        const ErrorDomainId JobDomain{"horo.foundation.jobs"};
        const ErrorDomainId MathDomain{"horo.foundation.math"};
    } // namespace

    namespace ConfigurationErrors
    {
        const ErrorCodeDescriptor SchemaInvalid{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.schema_invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration schema is invalid.",
            .remediationHint = "Correct the schema descriptor before registration.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor SchemaSealed{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.schema_sealed"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration schema is already sealed.",
            .remediationHint = "Register settings before sealing the schema.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor DraftStale{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.draft_stale"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration draft is stale.",
            .remediationHint = "Refresh the draft from the active configuration and retry.",
            .retryable = true,
            .userActionable = false
        };

        const ErrorCodeDescriptor ValueInvalid{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.value_invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration value is invalid.",
            .remediationHint = "Provide a value matching the registered setting type.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor JsonParseError{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.json_parse_error"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration JSON could not be parsed.",
            .remediationHint = "Repair or regenerate the configuration file.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor FileNotFound{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.file_not_found"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration file was not found.",
            .remediationHint = "Verify the configuration path.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor FileWriteError{
            .domain = ConfigurationDomain,
            .code = ErrorCode{"configuration.file_write_error"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Configuration file could not be written.",
            .remediationHint = "Verify destination permissions and available storage.",
            .retryable = true,
            .userActionable = true
        };
    } // namespace ConfigurationErrors

    namespace JobErrors
    {
        const ErrorCodeDescriptor Cancelled{
            .domain = JobDomain,
            .code = ErrorCode{"job.cancelled"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job was cancelled.",
            .remediationHint = "Retry only if the owning operation is still active.",
            .retryable = true,
            .userActionable = false
        };

        const ErrorCodeDescriptor Failed{
            .domain = JobDomain,
            .code = ErrorCode{"job.failed"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job execution failed.",
            .remediationHint = "Inspect the job error and diagnostics before retrying.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidHandle{
            .domain = JobDomain,
            .code = ErrorCode{"job.invalid_handle"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job handle is invalid.",
            .remediationHint = "Use a handle returned by the active job system.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor NotFound{
            .domain = JobDomain,
            .code = ErrorCode{"job.not_found"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job was not found.",
            .remediationHint = "Verify that the job belongs to the active job system.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor QueueFull{
            .domain = JobDomain,
            .code = ErrorCode{"job.queue_full"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job queue is full.",
            .remediationHint = "Retry after queued work completes.",
            .retryable = true,
            .userActionable = false
        };

        const ErrorCodeDescriptor Shutdown{
            .domain = JobDomain,
            .code = ErrorCode{"job.shutdown"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Job system is shutting down.",
            .remediationHint = "Do not submit new work after shutdown begins.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor TaskGroupClosed{
            .domain = JobDomain,
            .code = ErrorCode{"job.task_group_closed"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Task group is closed.",
            .remediationHint = "Spawn child work before cancelling or joining the task group.",
            .retryable = false,
            .userActionable = false
        };
    } // namespace JobErrors

    namespace HashingErrors
    {
        const ErrorCodeDescriptor InvalidSha256Text{
            .domain = HashingDomain,
            .code = ErrorCode{"foundation.sha256.invalid_text"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "SHA-256 text is not canonical.",
            .remediationHint = "Provide the lowercase sha256 prefix and exactly 64 lowercase hexadecimal digits.",
            .retryable = false,
            .userActionable = true
        };
    } // namespace HashingErrors

    namespace Math::Errors
    {
        const ErrorCodeDescriptor InvalidAffineMatrix{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_affine_matrix"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Matrix is not a valid affine transform.",
            .remediationHint = "Provide a finite affine matrix.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidBounds{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_bounds"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Bounds are invalid.",
            .remediationHint = "Provide finite ordered bounds.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidHomogeneousPoint{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_homogeneous_point"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Homogeneous point is invalid.",
            .remediationHint = "Use a finite point with a valid homogeneous divisor.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidPlane{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_plane"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Plane is invalid.",
            .remediationHint = "Provide a finite plane with a non-zero normal.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidProjection{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_projection"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Projection parameters are invalid.",
            .remediationHint = "Provide finite projection parameters with valid ranges.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidRay{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_ray"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Ray is invalid.",
            .remediationHint = "Provide a finite normalized direction and ordered distances.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor InvalidView{
            .domain = MathDomain,
            .code = ErrorCode{"math.invalid_view"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "View parameters are invalid.",
            .remediationHint = "Provide distinct finite eye and target vectors with a valid up vector.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor NonFiniteInput{
            .domain = MathDomain,
            .code = ErrorCode{"math.non_finite_input"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Math input contains a non-finite value.",
            .remediationHint = "Remove NaN or infinity values before the operation.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor SingularMatrix{
            .domain = MathDomain,
            .code = ErrorCode{"math.singular_matrix"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Matrix is singular.",
            .remediationHint = "Provide an invertible matrix.",
            .retryable = false,
            .userActionable = false
        };

        const ErrorCodeDescriptor ZeroLength{
            .domain = MathDomain,
            .code = ErrorCode{"math.zero_length"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Vector length is zero.",
            .remediationHint = "Provide a vector with non-zero length.",
            .retryable = false,
            .userActionable = false
        };
    } // namespace Math::Errors

    namespace
    {
        const ErrorDomainId PathDomain{"horo.foundation.paths"};
    } // namespace

    namespace PathErrors
    {
        const ErrorCodeDescriptor DirectoryCreateFailed{
            .domain = PathDomain,
            .code = ErrorCode{"paths.directory_create_failed"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Failed to create directory.",
            .remediationHint = "Verify parent directory permissions and available storage.",
            .retryable = true,
            .userActionable = true
        };

        const ErrorCodeDescriptor PathEscape{
            .domain = PathDomain,
            .code = ErrorCode{"paths.escape"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Path escapes the allowed root.",
            .remediationHint = "Use a path that stays within the project directory.",
            .retryable = false,
            .userActionable = true
        };

        const ErrorCodeDescriptor InvalidPath{
            .domain = PathDomain,
            .code = ErrorCode{"paths.invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "Path is invalid or malformed.",
            .remediationHint = "Provide a valid path without illegal characters or empty segments.",
            .retryable = false,
            .userActionable = true
        };
    } // namespace PathErrors
} // namespace Horo
