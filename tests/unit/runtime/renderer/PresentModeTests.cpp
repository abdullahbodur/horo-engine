#include "Horo/Runtime/Render/PresentMode.h"
#include "Horo/Runtime/Render/PresentModeErrors.h"
#include "Horo/Runtime/Render/RenderBackend.h"

#include <catch2/catch_test_macros.hpp>
#include <utility>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Render;

    void CheckError(const Error &error, const ErrorCodeDescriptor &descriptor) {
        CHECK(error.domain.Value() == descriptor.domain.Value());
        CHECK(error.code.Value() == descriptor.code.Value());
    }

    [[nodiscard]] PresentModeCapabilities AllModes() {
        return {{PresentMode::Fifo, PresentMode::Immediate}};
    }
}  // namespace

TEST_CASE("Explicit present-mode negotiation requires an exact supported mode", "[runtime][renderer][present-mode]") {
    const PresentModeRequest request{PresentModeRequestKind::Required, {PresentMode::Immediate}};
    const auto exact = NegotiatePresentMode(request, AllModes());
    REQUIRE(exact.HasValue());
    CHECK(exact.Value() == ResolvedPresentMode{PresentMode::Immediate, PresentMode::Immediate, PresentModeResolution::Exact, 0});

    const auto unsupported = NegotiatePresentMode(request, {{PresentMode::Fifo}});
    REQUIRE(unsupported.HasError());
    CheckError(unsupported.ErrorValue(), PresentModeErrors::RequiredModeUnavailable);
}

TEST_CASE("Auto present-mode negotiation follows only host preference order", "[runtime][renderer][present-mode]") {
    const PresentModeRequest immediateFirst{PresentModeRequestKind::Auto, {PresentMode::Immediate, PresentMode::Fifo}};
    const auto exact = NegotiatePresentMode(immediateFirst, AllModes());
    REQUIRE(exact.HasValue());
    CHECK(exact.Value() == ResolvedPresentMode{PresentMode::Immediate, PresentMode::Immediate, PresentModeResolution::Exact, 0});

    const auto degraded = NegotiatePresentMode(immediateFirst, {{PresentMode::Fifo}});
    REQUIRE(degraded.HasValue());
    CHECK(degraded.Value() == ResolvedPresentMode{PresentMode::Immediate, PresentMode::Fifo, PresentModeResolution::DegradedFallback, 1});

    const PresentModeRequest fifoFirst{PresentModeRequestKind::Auto, {PresentMode::Fifo, PresentMode::Immediate}};
    const auto hostOrdered = NegotiatePresentMode(fifoFirst, AllModes());
    REQUIRE(hostOrdered.HasValue());
    CHECK(hostOrdered.Value().resolved == PresentMode::Fifo);
    CHECK(hostOrdered.Value().preferenceIndex == 0);
}

TEST_CASE("Auto present-mode negotiation fails when no declared preference matches", "[runtime][renderer][present-mode]") {
    const PresentModeRequest request{PresentModeRequestKind::Auto, {PresentMode::Immediate}};
    const auto result = NegotiatePresentMode(request, {{PresentMode::Fifo}});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::NoPreferredModeAvailable);
}

TEST_CASE("Present-mode requests reject empty duplicate unknown and over-bound input", "[runtime][renderer][present-mode]") {
    auto result = NegotiatePresentMode({PresentModeRequestKind::Auto, {}}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidRequest);

    result = NegotiatePresentMode({PresentModeRequestKind::Auto, {PresentMode::Fifo, PresentMode::Fifo}}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidRequest);

    result = NegotiatePresentMode({PresentModeRequestKind::Required, {PresentMode::Fifo, PresentMode::Immediate}}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidRequest);

    result = NegotiatePresentMode({static_cast<PresentModeRequestKind>(255), {PresentMode::Fifo}}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::UnsupportedModeFact);

    result = NegotiatePresentMode({PresentModeRequestKind::Required, {static_cast<PresentMode>(255)}}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::UnsupportedModeFact);

    std::vector<PresentMode> overBound(MaximumPresentModeEntries + 1, PresentMode::Fifo);
    result = NegotiatePresentMode({PresentModeRequestKind::Auto, std::move(overBound)}, AllModes());
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidRequest);
}

TEST_CASE("Present-mode capabilities require canonical bounded known modes", "[runtime][renderer][present-mode]") {
    const PresentModeRequest request{PresentModeRequestKind::Required, {PresentMode::Fifo}};
    auto result = NegotiatePresentMode(request, {{}});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidCapabilities);

    result = NegotiatePresentMode(request, {{PresentMode::Fifo, PresentMode::Fifo}});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidCapabilities);

    result = NegotiatePresentMode(request, {{PresentMode::Immediate, PresentMode::Fifo}});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidCapabilities);

    result = NegotiatePresentMode(request, {{static_cast<PresentMode>(255)}});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::UnsupportedModeFact);

    std::vector<PresentMode> overBound(MaximumPresentModeEntries + 1, PresentMode::Fifo);
    result = NegotiatePresentMode(request, {std::move(overBound)});
    REQUIRE(result.HasError());
    CheckError(result.ErrorValue(), PresentModeErrors::InvalidCapabilities);
}

TEST_CASE("Canonical negotiation is deterministic and preserves RenderBackendConfig compatibility", "[runtime][renderer][present-mode]") {
    const PresentModeRequest request{PresentModeRequestKind::Auto, {PresentMode::Immediate, PresentMode::Fifo}};
    const PresentModeCapabilities capabilities = AllModes();
    const auto first = NegotiatePresentMode(request, capabilities);
    const auto second = NegotiatePresentMode(request, capabilities);
    REQUIRE(first.HasValue());
    REQUIRE(second.HasValue());
    CHECK(first.Value() == second.Value());

    CHECK(RenderBackendConfig{.presentMode = PresentMode::Fifo}.IsValid());
    CHECK(RenderBackendConfig{.presentMode = PresentMode::Immediate}.IsValid());
    CHECK_FALSE(RenderBackendConfig{.presentMode = static_cast<PresentMode>(255)}.IsValid());
}
