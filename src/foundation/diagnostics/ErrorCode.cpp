#include "Horo/Foundation/ErrorCode.h"

#include <utility>

namespace Horo
{
/** @copydoc MakeError */
Error MakeError(const ErrorCodeDescriptor &descriptor, std::string message)
{
    if (message.empty())
        message.assign(descriptor.summary);

    return Error{
        .code = descriptor.code,
        .domain = descriptor.domain,
        .severity = descriptor.defaultSeverity,
        .message = std::move(message),
    };
}
} // namespace Horo
