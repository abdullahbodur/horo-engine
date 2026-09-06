#pragma once

#include "Horo/Foundation/Result.h"

namespace Horo::WorldStreaming::Internal {
    template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
        return Result<T>::Failure(MakeError(descriptor));
    }
}  // namespace Horo::WorldStreaming::Internal
