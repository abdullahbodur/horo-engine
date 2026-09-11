#pragma once

/**
 * @file AIErrors.h
 * @brief Stable gameplay-AI contract errors independent of runtime storage.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::AI::AIErrors {
    /** @brief A persistent AI identity uses its reserved zero representation. */
    extern const ErrorCodeDescriptor IdentityInvalid;
    /** @brief Two descriptors collide within one persistent identity domain. */
    extern const ErrorCodeDescriptor DescriptorConflict;
    /** @brief A descriptor set exceeds its bounded validation capacity. */
    extern const ErrorCodeDescriptor DescriptorLimitExceeded;
    /** @brief A runtime handle is malformed or belongs to another scene-runtime incarnation. */
    extern const ErrorCodeDescriptor HandleInvalid;
    /** @brief A runtime slot generation cannot advance without wrapping. */
    extern const ErrorCodeDescriptor GenerationExhausted;
    /** @brief A blackboard schema or key descriptor has an invalid representation. */
    extern const ErrorCodeDescriptor BlackboardSchemaInvalid;
    /** @brief A blackboard schema or collection exceeds its fixed contract capacity. */
    extern const ErrorCodeDescriptor BlackboardLimitExceeded;
    /** @brief A blackboard value does not match its admitted key type. */
    extern const ErrorCodeDescriptor BlackboardValueTypeMismatch;
    /** @brief A blackboard value is non-finite, malformed, or an invalid stored reference. */
    extern const ErrorCodeDescriptor BlackboardValueInvalid;
    /** @brief An unavailable key or value type was supplied under a rejecting schema policy. */
    extern const ErrorCodeDescriptor BlackboardUnknownValueRejected;
    /** @brief Immutable blackboard schema or instance storage could not be allocated. */
    extern const ErrorCodeDescriptor BlackboardStorageUnavailable;
    /** @brief Blackboard runtime/schema/agent generation is stale or foreign. */
    extern const ErrorCodeDescriptor BlackboardInstanceStale;
    /** @brief Blackboard instance binding or required default layout is invalid. */
    extern const ErrorCodeDescriptor BlackboardInstanceInvalid;
    /** @brief Blackboard write batch is duplicate, oversized, or revision-stale. */
    extern const ErrorCodeDescriptor BlackboardBatchInvalid;
    /** @brief Blackboard revision or instance generation cannot advance without wrapping. */
    extern const ErrorCodeDescriptor BlackboardRevisionExhausted;
    /** @brief A blackboard observer registration or generation-fenced token is invalid. */
    extern const ErrorCodeDescriptor BlackboardObserverInvalid;
    /** @brief A blackboard instance has no free bounded observer slot. */
    extern const ErrorCodeDescriptor BlackboardObserverLimitExceeded;
    /** @brief A publication callback attempted to mutate its blackboard or observer registry. */
    extern const ErrorCodeDescriptor BlackboardReentrantMutation;
}  // namespace Horo::AI::AIErrors
