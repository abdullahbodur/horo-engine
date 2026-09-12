#pragma once

/**
 * @file RenderDisplayErrors.h
 * @brief Stable typed errors for display capability snapshot publication.
 */

#include "Horo/Foundation/ErrorCode.h"

namespace Horo::Render::RenderDisplayErrors {
    extern const ErrorCodeDescriptor InvalidSnapshot;
    extern const ErrorCodeDescriptor UnsupportedFact;
    extern const ErrorCodeDescriptor StaleRevision;
    extern const ErrorCodeDescriptor RevisionUnchanged;
    extern const ErrorCodeDescriptor ThreadAffinityViolation;
    extern const ErrorCodeDescriptor FeedStopped;
    extern const ErrorCodeDescriptor SnapshotUnavailable;
}  // namespace Horo::Render::RenderDisplayErrors
