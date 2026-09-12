#pragma once

#include "Horo/Extensions/BackendServiceRegistry.h"

#include <atomic>
#include <memory>
#include <string>

namespace Horo::Extensions::Fixtures {
    struct SumRequest final {
        int left{};
        int right{};
    };

    struct SumResponse final {
        int value{};
    };

    struct ArithmeticServiceAudit final {
        std::string observedProvider;
        std::atomic_int shutdownCount{};
    };

    /** @brief Backend-only reference contract used by host and headless integration tests. */
    class ArithmeticService final {
    public:
        explicit ArithmeticService(std::shared_ptr<ArithmeticServiceAudit> audit) noexcept;
        [[nodiscard]] Result<SumResponse> Add(const SumRequest &request, const BackendServiceCallContext &context);
        [[nodiscard]] Result<SumResponse> Fail(const SumRequest &request, const BackendServiceCallContext &context) const;
        void Shutdown() noexcept;

    private:
        std::shared_ptr<ArithmeticServiceAudit> audit_;
    };
}  // namespace Horo::Extensions::Fixtures
