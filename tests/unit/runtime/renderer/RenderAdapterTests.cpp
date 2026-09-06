#include "Horo/Runtime/Render/RenderAdapter.h"
#include "Horo/Runtime/Render/RenderAdapterErrors.h"
#include "Horo/Runtime/Render/RenderBackend.h"

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>

namespace {
    using namespace Horo;
    using namespace Horo::Render;

    [[nodiscard]] RenderAdapterProperties Adapter(std::string id, const RenderAdapterKind kind = RenderAdapterKind::Integrated,
                                                  const RenderAdapterAvailability availability = RenderAdapterAvailability::Available,
                                                  const bool presentation = true) {
        return {RenderAdapterId{std::move(id)}, "Test adapter", kind, availability, 1024, presentation};
    }

    void CheckError(const Error &error, const ErrorCodeDescriptor &descriptor) {
        CHECK(error.domain.Value() == descriptor.domain.Value());
        CHECK(error.code.Value() == descriptor.code.Value());
    }

    class TrackingDiscovery final : public IRenderAdapterDiscovery {
    public:
        [[nodiscard]] Result<RenderAdapterSnapshot> Discover(const RenderAdapterDiscoveryRequest &request) override {
            if (stopped_) {
                return Result<RenderAdapterSnapshot>::Failure(MakeError(RenderAdapterErrors::DiscoveryStopped));
            }
            if (!request.IsValid()) {
                return Result<RenderAdapterSnapshot>::Failure(MakeError(RenderAdapterErrors::InvalidDiscoveryRequest));
            }
            return Result<RenderAdapterSnapshot>::Success(RenderAdapterSnapshot{1, {Adapter("adapter-1")}});
        }

        void Stop() noexcept override {
            stopped_ = true;
        }

    private:
        bool stopped_{false};
    };
}  // namespace

TEST_CASE("Adapter identities and discovery inputs enforce finite public bounds", "[runtime][renderer][adapter]") {
    CHECK_FALSE(RenderAdapterId{}.IsValid());
    CHECK(RenderAdapterId{"vendor:device-1"}.IsValid());
    CHECK_FALSE(RenderAdapterId{"device name"}.IsValid());
    CHECK_FALSE(RenderAdapterId{std::string(129, 'a')}.IsValid());

    STATIC_CHECK_FALSE(RenderAdapterDiscoveryRequest{0}.IsValid());
    STATIC_CHECK(RenderAdapterDiscoveryRequest{64}.IsValid());
    STATIC_CHECK_FALSE(RenderAdapterDiscoveryRequest{65}.IsValid());
}

TEST_CASE("Adapter snapshots require canonical unique identity order", "[runtime][renderer][adapter]") {
    CHECK(RenderAdapterSnapshot{7, {Adapter("a"), Adapter("b")}}.IsValid());
    CHECK_FALSE(RenderAdapterSnapshot{0, {Adapter("a")}}.IsValid());
    CHECK_FALSE(RenderAdapterSnapshot{7, {Adapter("b"), Adapter("a")}}.IsValid());
    CHECK_FALSE(RenderAdapterSnapshot{7, {Adapter("a"), Adapter("a")}}.IsValid());
    CHECK_FALSE(RenderAdapterSnapshot{7, {Adapter("bad id")}}.IsValid());
    auto embeddedNull = Adapter("valid");
    embeddedNull.displayName = std::string{"bad\0name", 8};
    CHECK_FALSE(RenderAdapterSnapshot{7, {embeddedNull}}.IsValid());
    CHECK_FALSE(Adapter("valid", static_cast<RenderAdapterKind>(255)).IsValid());
    auto invalidAvailability = Adapter("valid");
    invalidAvailability.availability = static_cast<RenderAdapterAvailability>(255);
    CHECK_FALSE(invalidAvailability.IsValid());
}

TEST_CASE("Selection is deterministic and never chooses unavailable or software adapters implicitly", "[runtime][renderer][adapter]") {
    const RenderAdapterSnapshot snapshot{12,
                                         {Adapter("a-unavailable", RenderAdapterKind::Discrete, RenderAdapterAvailability::Unavailable),
                                          Adapter("b-software", RenderAdapterKind::Software), Adapter("c-integrated"),
                                          Adapter("d-discrete", RenderAdapterKind::Discrete)}};

    const auto defaultSelection = SelectRenderAdapter(snapshot, {});
    REQUIRE(defaultSelection.HasValue());
    CHECK(defaultSelection.Value().adapter.id == RenderAdapterId{"c-integrated"});
    CHECK(defaultSelection.Value().discoveryRevision == 12);

    const auto discrete = SelectRenderAdapter(snapshot, {.requiredKind = RenderAdapterKind::Discrete});
    REQUIRE(discrete.HasValue());
    CHECK(discrete.Value().adapter.id == RenderAdapterId{"d-discrete"});

    const auto software = SelectRenderAdapter(snapshot, {.requiredKind = RenderAdapterKind::Software, .allowSoftware = true});
    REQUIRE(software.HasValue());
    CHECK(software.Value().adapter.id == RenderAdapterId{"b-software"});
}

TEST_CASE("Explicit adapter selection preserves distinct typed failure causes", "[runtime][renderer][adapter]") {
    const RenderAdapterSnapshot snapshot{3,
                                         {Adapter("headless", RenderAdapterKind::Integrated, RenderAdapterAvailability::Available, false),
                                          Adapter("offline", RenderAdapterKind::Discrete, RenderAdapterAvailability::Unavailable)}};

    const auto missing = SelectRenderAdapter(snapshot, {.requiredAdapter = RenderAdapterId{"missing"}});
    REQUIRE(missing.HasError());
    CheckError(missing.ErrorValue(), RenderAdapterErrors::RequiredAdapterNotFound);

    const auto unavailable = SelectRenderAdapter(snapshot, {.requiredAdapter = RenderAdapterId{"offline"}});
    REQUIRE(unavailable.HasError());
    CheckError(unavailable.ErrorValue(), RenderAdapterErrors::AdapterUnavailable);

    const auto incompatible = SelectRenderAdapter(snapshot, {.requiredAdapter = RenderAdapterId{"headless"}, .requirePresentation = true});
    REQUIRE(incompatible.HasError());
    CheckError(incompatible.ErrorValue(), RenderAdapterErrors::NoCompatibleAdapter);
}

TEST_CASE("Malformed selection and discovery results fail before selection", "[runtime][renderer][adapter]") {
    const auto invalidSnapshot = SelectRenderAdapter(RenderAdapterSnapshot{}, {});
    REQUIRE(invalidSnapshot.HasError());
    CheckError(invalidSnapshot.ErrorValue(), RenderAdapterErrors::InvalidSnapshot);

    const RenderAdapterSnapshot snapshot{1, {Adapter("valid")}};
    const auto invalidRequest = SelectRenderAdapter(snapshot, {.requiredAdapter = RenderAdapterId{"bad id"}});
    REQUIRE(invalidRequest.HasError());
    CheckError(invalidRequest.ErrorValue(), RenderAdapterErrors::InvalidSelectionRequest);

    const auto invalidKind = SelectRenderAdapter(snapshot, {.requiredKind = static_cast<RenderAdapterKind>(255)});
    REQUIRE(invalidKind.HasError());
    CheckError(invalidKind.ErrorValue(), RenderAdapterErrors::InvalidSelectionRequest);

    const auto noMatch = SelectRenderAdapter(snapshot, {.requiredKind = RenderAdapterKind::Discrete});
    REQUIRE(noMatch.HasError());
    CheckError(noMatch.ErrorValue(), RenderAdapterErrors::NoCompatibleAdapter);
}

TEST_CASE("Device creation diagnostics remain bounded typed and native-free", "[runtime][renderer][adapter]") {
    CHECK(RenderDeviceCreationFailure{RenderAdapterId{"gpu-1"}, RenderDeviceCreationFailureKind::DriverRejected,
                                      "Driver rejected required device features", false}
              .IsValid());
    CHECK_FALSE(RenderDeviceCreationFailure{{}, RenderDeviceCreationFailureKind::DriverRejected, "failure", false}.IsValid());
    CHECK_FALSE(RenderDeviceCreationFailure{RenderAdapterId{"gpu-1"}, static_cast<RenderDeviceCreationFailureKind>(255), "failure", false}
                    .IsValid());
    CHECK_FALSE(RenderDeviceCreationFailure{RenderAdapterId{"gpu-1"}, RenderDeviceCreationFailureKind::Unknown, {}, false}.IsValid());
    CHECK_FALSE(
        RenderDeviceCreationFailure{RenderAdapterId{"gpu-1"}, RenderDeviceCreationFailureKind::Unknown, std::string(1025, 'x'), false}
            .IsValid());
}

TEST_CASE("Backend config pairs explicit adapter identity with its discovery revision", "[runtime][renderer][adapter]") {
    CHECK(RenderBackendConfig{}.IsValid());
    CHECK(RenderBackendConfig{.adapter = RenderAdapterId{"gpu-1"}, .adapterDiscoveryRevision = 4}.IsValid());
    CHECK_FALSE(RenderBackendConfig{.adapter = RenderAdapterId{"gpu-1"}}.IsValid());
    CHECK_FALSE(RenderBackendConfig{.adapterDiscoveryRevision = 4}.IsValid());
    CHECK_FALSE(RenderBackendConfig{.adapter = RenderAdapterId{"bad id"}, .adapterDiscoveryRevision = 4}.IsValid());
}

TEST_CASE("Discovery ownership closes admission idempotently before shutdown", "[runtime][renderer][adapter]") {
    TrackingDiscovery discovery;
    REQUIRE(discovery.Discover(RenderAdapterDiscoveryRequest{1}).HasValue());

    discovery.Stop();
    discovery.Stop();

    const auto stopped = discovery.Discover(RenderAdapterDiscoveryRequest{1});
    REQUIRE(stopped.HasError());
    CheckError(stopped.ErrorValue(), RenderAdapterErrors::DiscoveryStopped);
}
