#include "Horo/Runtime/Ui/UiIdentity.h"

namespace Horo::Runtime::Ui {
    /** @copydoc UiOwnershipGeneration::Create */
    Result<UiOwnershipGeneration> UiOwnershipGeneration::Create(const std::uint64_t value) {
        if (value == 0)
            return Result<UiOwnershipGeneration>::Failure(MakeError(UiErrors::OwnershipGenerationInvalid));
        return Result<UiOwnershipGeneration>::Success(UiOwnershipGeneration{value});
    }
}  // namespace Horo::Runtime::Ui
