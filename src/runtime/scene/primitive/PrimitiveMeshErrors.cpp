#include "PrimitiveMeshErrors.h"

namespace Horo::Runtime::PrimitiveErrors
{
namespace
{
const ErrorDomainId Domain{"horo.runtime.scene.primitives"};

[[nodiscard]] ErrorCodeDescriptor Describe(const char *code, const char *summary, const char *remediation,
                                           const bool retryable = false)
{
    return ErrorCodeDescriptor{.domain = Domain,
                               .code = ErrorCode{code},
                               .defaultSeverity = ErrorSeverity::Error,
                               .summary = summary,
                               .remediationHint = remediation,
                               .retryable = retryable,
                               .userActionable = false};
}
} // namespace

const ErrorCodeDescriptor CacheCapacityExceeded =
    Describe("primitive.cache_capacity_exceeded", "Primitive mesh cache capacity was exceeded.",
             "Release active mesh leases or increase the configured cache budget.", true);
const ErrorCodeDescriptor CapacityExceeded =
    Describe("primitive.capacity_exceeded", "Primitive generation capacity was exceeded.",
             "Reduce tessellation or generated mesh dimensions.");
const ErrorCodeDescriptor InvalidCapsuleHeight =
    Describe("primitive.invalid_capsule_height", "Capsule height is invalid.",
             "Use a total height greater than or equal to the capsule diameter.");
const ErrorCodeDescriptor InvalidDimensions =
    Describe("primitive.invalid_dimensions", "Primitive dimensions are invalid.",
             "Use finite positive dimensions.");
const ErrorCodeDescriptor InvalidGeneratedMesh =
    Describe("primitive.invalid_generated_mesh", "Primitive generation produced invalid mesh data.",
             "Inspect the primitive generator and input descriptor.");
const ErrorCodeDescriptor InvalidRadius =
    Describe("primitive.invalid_radius", "Primitive radius is invalid.", "Use a finite positive radius.");
const ErrorCodeDescriptor InvalidSize =
    Describe("primitive.invalid_size", "Primitive size is invalid.", "Use finite positive size components.");
const ErrorCodeDescriptor InvalidTessellation =
    Describe("primitive.invalid_tessellation", "Primitive tessellation is invalid.",
             "Use tessellation values within the supported range.");
const ErrorCodeDescriptor ParameterMismatch =
    Describe("primitive.parameter_mismatch", "Primitive type and parameter payload do not match.",
             "Provide the parameter type required by the primitive descriptor.");
const ErrorCodeDescriptor UnsupportedVersion =
    Describe("primitive.unsupported_version", "Primitive mesh descriptor version is unsupported.",
             "Migrate the descriptor to a supported version.");
} // namespace Horo::Runtime::PrimitiveErrors
