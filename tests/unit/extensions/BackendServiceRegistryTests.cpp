#include "BackendServiceReference.h"
#include "Horo/Extensions/BackendServiceRegistry.h"
#include "Horo/Extensions/ExtensionErrors.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Extensions::Tests {
    namespace {
        using Fixtures::ArithmeticService;
        using Fixtures::ArithmeticServiceAudit;
        using Fixtures::SumRequest;
        using Fixtures::SumResponse;

        class DrainingService final {
        public:
            struct Audit final {
                std::atomic_bool entered{};
                std::atomic_bool exited{};
                std::atomic_bool shutdownAfterExit{};
            };

            explicit DrainingService(std::shared_ptr<Audit> audit) noexcept : audit_(std::move(audit)) {}

            [[nodiscard]] Result<SumResponse> WaitForCancellation(const SumRequest &, const BackendServiceCallContext &context) {
                audit_->entered.store(true, std::memory_order_release);
                while (!context.IsCancellationRequested())
                    std::this_thread::yield();
                audit_->exited.store(true, std::memory_order_release);
                return Result<SumResponse>::Failure(MakeError(ExtensionErrors::BackendServiceCancelled));
            }

            void Shutdown() noexcept {
                audit_->shutdownAfterExit.store(audit_->exited.load(std::memory_order_acquire), std::memory_order_release);
            }

        private:
            std::shared_ptr<Audit> audit_;
        };

        class OtherService final {
        public:
            void Shutdown() noexcept {}
        };

        class OrderedShutdownService final {
        public:
            OrderedShutdownService(std::shared_ptr<std::vector<int>> order, const int value) noexcept
                : order_(std::move(order)), value_(value) {}

            void Shutdown() noexcept {
                order_->push_back(value_);
            }

        private:
            std::shared_ptr<std::vector<int>> order_;
            int value_{};
        };

        class NoopService final {
        public:
            void Shutdown() noexcept {}
        };

        class OwnerThreadShutdownService final {
        public:
            struct Audit final {
                std::atomic_int shutdownCount{};
                std::thread::id shutdownThread;
            };

            explicit OwnerThreadShutdownService(std::shared_ptr<Audit> audit) noexcept : audit_(std::move(audit)) {}

            void Shutdown() noexcept {
                audit_->shutdownThread = std::this_thread::get_id();
                ++audit_->shutdownCount;
            }

        private:
            std::shared_ptr<Audit> audit_;
        };

        class SelfStoppingService final {
        public:
            SelfStoppingService(BackendServiceRegistry &registry, std::shared_ptr<std::atomic_int> shutdownCount) noexcept
                : registry_(registry), shutdownCount_(std::move(shutdownCount)) {}

            [[nodiscard]] Result<SumResponse> Stop(const SumRequest &, const BackendServiceCallContext &) {
                static_cast<void>(registry_.BeginShutdown());
                return Result<SumResponse>::Success({42});
            }

            void Shutdown() noexcept {
                ++*shutdownCount_;
            }

        private:
            BackendServiceRegistry &registry_;
            std::shared_ptr<std::atomic_int> shutdownCount_;
        };

        class UnresponsiveService final {
        public:
            struct Audit final {
                std::atomic_bool entered{};
                std::atomic_bool release{};
                std::atomic_int shutdownCount{};
            };

            explicit UnresponsiveService(std::shared_ptr<Audit> audit) noexcept : audit_(std::move(audit)) {}

            [[nodiscard]] Result<SumResponse> IgnoreCancellation(const SumRequest &, const BackendServiceCallContext &) {
                audit_->entered.store(true, std::memory_order_release);
                while (!audit_->release.load(std::memory_order_acquire))
                    std::this_thread::yield();
                return Result<SumResponse>::Success({42});
            }

            void Shutdown() noexcept {
                ++audit_->shutdownCount;
            }

        private:
            std::shared_ptr<Audit> audit_;
        };

        struct NestedCallControl final {
            std::function<Result<SumResponse>()> invokeInner;
            std::atomic_int shutdownCount{};
        };

        class NestedSelfStoppingService final {
        public:
            NestedSelfStoppingService(BackendServiceRegistry &registry, std::shared_ptr<NestedCallControl> control) noexcept
                : registry_(registry), control_(std::move(control)) {}

            [[nodiscard]] Result<SumResponse> Outer(const SumRequest &, const BackendServiceCallContext &) {
                return control_->invokeInner();
            }

            [[nodiscard]] Result<SumResponse> Inner(const SumRequest &, const BackendServiceCallContext &) {
                static_cast<void>(registry_.BeginShutdown());
                return Result<SumResponse>::Success({42});
            }

            void Shutdown() noexcept {
                ++control_->shutdownCount;
            }

        private:
            BackendServiceRegistry &registry_;
            std::shared_ptr<NestedCallControl> control_;
        };

        [[nodiscard]] BackendServiceDescriptor Descriptor(const BackendServiceThreadRule threadRule = BackendServiceThreadRule::AnyThread) {
            return {.serviceId = {"com.example.math"},
                    .contractId = {"com.example.math.v1"},
                    .capability = {"com.example.math.use"},
                    .version = {1, 0, 0},
                    .providerId = "com.example.math-provider",
                    .providerGeneration = 7,
                    .threadRule = threadRule};
        }

        [[nodiscard]] ExtensionCapabilityAdmission Admission() {
            ExtensionAdmissionPolicy policy{.revision = 2, .availableCapabilities = {{"com.example.math.use"}}};
            ExtensionAdmissionRequest request{.extensionId = "com.example.consumer",
                                              .moduleId = "com.example.consumer.backend",
                                              .activationGeneration = 3,
                                              .capabilities = {{{"com.example.math.use"}, {}}}};
            auto admission = ExtensionCapabilityAdmission::Evaluate(request, policy);
            REQUIRE(admission.HasValue());
            return std::move(admission).Value();
        }

        [[nodiscard]] ApplicationCapabilityProviderLease CapabilityLease(ApplicationCapabilityRegistry &registry,
                                                                         ExtensionCapabilityAdmission &admission) {
            auto handle = admission.Grant({"com.example.math.use"});
            REQUIRE(handle.HasValue());
            auto lease =
                registry.Resolve(handle.Value(), {{1, 0, 0}, {1, 0, 0}}, "com.example.consumer", "com.example.consumer.backend", 3);
            REQUIRE(lease.HasValue());
            return std::move(lease).Value();
        }

        void RequireErrorCode(const auto &result, const std::string &code) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().code.Value() == code);
        }

        struct Fixture final {
            Fixture() {
                auto published = capabilities.Register({{"com.example.math.use"}, {1, 0, 0}, "com.example.math-provider", 7});
                REQUIRE(published.HasValue());
                capabilityRegistration.emplace(std::move(published).Value());
            }

            ApplicationCapabilityRegistry capabilities;
            std::optional<ApplicationCapabilityProviderRegistration> capabilityRegistration;
            ExtensionCapabilityAdmission admission = Admission();
            BackendServiceRegistry services;
        };
    }  // namespace

    TEST_CASE("Backend-only service performs typed attributed calls without presentation dependencies", "[Extensions][BackendService]") {
        Fixture fixture;
        auto audit = std::make_shared<ArithmeticServiceAudit>();
        auto registration = fixture.services.Register(Descriptor(), std::make_unique<ArithmeticService>(audit));
        REQUIRE(registration.HasValue());
        auto call = fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(call.HasValue());
        auto response = std::move(call).Value().Invoke(&ArithmeticService::Add, SumRequest{20, 22});
        REQUIRE(response.HasValue());
        CHECK(response.Value().value == 42);
        CHECK(audit->observedProvider == "com.example.math-provider");
    }

    TEST_CASE("Backend service preserves typed provider errors and caller cancellation", "[Extensions][BackendService]") {
        Fixture fixture;
        auto registration =
            fixture.services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()));
        REQUIRE(registration.HasValue());
        auto failure = fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                   {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(failure.HasValue());
        auto failed = std::move(failure).Value().Invoke(&ArithmeticService::Fail, SumRequest{});
        RequireErrorCode(failed, "backend_service_invocation_failed");
        REQUIRE(failed.ErrorValue().cause);
        CHECK(failed.ErrorValue().cause.Get()->code.Value() == "contribution_rejected");

        CancellationSource cancellation;
        cancellation.RequestCancellation();
        auto cancelled = fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                     {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(cancelled.HasValue());
        RequireErrorCode(std::move(cancelled).Value().Invoke(&ArithmeticService::Add, SumRequest{}, cancellation.Token()),
                         "backend_service_cancelled");
    }

    TEST_CASE("Backend service rejects mismatched contracts types and threads before provider invocation", "[Extensions][BackendService]") {
        Fixture fixture;
        auto audit = std::make_shared<ArithmeticServiceAudit>();
        auto registration = fixture.services.Register(Descriptor(BackendServiceThreadRule::ProviderOwnerThread),
                                                      std::make_unique<ArithmeticService>(audit));
        REQUIRE(registration.HasValue());
        RequireErrorCode(fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                     {"com.example.math"}, {"com.example.other.v1"}),
                         "backend_service_contract_mismatch");
        RequireErrorCode(fixture.services.Resolve<OtherService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                {"com.example.math"}, {"com.example.math.v1"}),
                         "backend_service_type_mismatch");
        auto call = fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(call.HasValue());
        auto wrongThread = std::async(std::launch::async, [call = std::move(call).Value()]() mutable {
            return std::move(call).Invoke(&ArithmeticService::Add, SumRequest{});
        });
        RequireErrorCode(wrongThread.get(), "backend_service_thread_violation");
        CHECK(audit->observedProvider.empty());
    }

    TEST_CASE("Backend service revocation cancels and drains work before shutdown", "[Extensions][BackendService]") {
        Fixture fixture;
        auto audit = std::make_shared<DrainingService::Audit>();
        auto published = fixture.services.Register<DrainingService>(Descriptor(), std::make_unique<DrainingService>(audit));
        REQUIRE(published.HasValue());
        BackendServiceRegistration registration = std::move(published).Value();
        auto call = fixture.services.Resolve<DrainingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                              {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(call.HasValue());
        auto work = std::async(std::launch::async, [call = std::move(call).Value()]() mutable {
            return std::move(call).Invoke(&DrainingService::WaitForCancellation, SumRequest{});
        });
        while (!audit->entered.load(std::memory_order_acquire))
            std::this_thread::yield();
        REQUIRE(registration.Reset().HasValue());
        CHECK(audit->exited.load(std::memory_order_acquire));
        CHECK(audit->shutdownAfterExit.load(std::memory_order_acquire));
        RequireErrorCode(work.get(), "backend_service_cancelled");
        CHECK_FALSE(registration.IsRegistered());
        RequireErrorCode(fixture.services.Resolve<DrainingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                   {"com.example.math"}, {"com.example.math.v1"}),
                         "backend_service_unavailable");
    }

    TEST_CASE("Backend service owner-thread shutdown is finalized on the provider thread", "[Extensions][BackendService]") {
        Fixture fixture;
        const std::thread::id ownerThread = std::this_thread::get_id();
        auto audit = std::make_shared<OwnerThreadShutdownService::Audit>();
        auto published = fixture.services.Register(Descriptor(BackendServiceThreadRule::ProviderOwnerThread),
                                                   std::make_unique<OwnerThreadShutdownService>(audit));
        REQUIRE(published.HasValue());
        BackendServiceRegistration registration = std::move(published).Value();

        auto wrongThread = std::async(std::launch::async, [&registration] {
            return registration.Reset();
        });
        const auto retired = wrongThread.get();
        REQUIRE(retired.HasValue());
        CHECK(retired.Value() == BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired);
        CHECK_FALSE(registration.IsRegistered());
        CHECK(audit->shutdownCount.load() == 0);

        const auto finalized = fixture.services.FinalizeRetiredOnOwnerThread();
        REQUIRE(finalized.HasValue());
        CHECK(finalized.Value() == BackendServiceRetirementDisposition::ShutdownComplete);
        CHECK(audit->shutdownCount.load() == 1);
        CHECK(audit->shutdownThread == ownerThread);
    }

    TEST_CASE("Backend service registry validates publication and shuts down terminally", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        auto invalid = Descriptor();
        invalid.providerGeneration = 0;
        RequireErrorCode(services.Register(std::move(invalid),
                                           std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>())),
                         "backend_service_invalid");
        auto audit = std::make_shared<ArithmeticServiceAudit>();
        auto registration = services.Register(Descriptor(), std::make_unique<ArithmeticService>(audit));
        REQUIRE(registration.HasValue());
        RequireErrorCode(services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>())),
                         "backend_service_duplicate");
        REQUIRE(services.BeginShutdown().HasValue());
        CHECK(services.IsShutdown());
        CHECK(audit->shutdownCount.load() == 1);
        CHECK_FALSE(registration.Value().IsRegistered());
        RequireErrorCode(services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>())),
                         "backend_service_shutdown");
    }

    TEST_CASE("Backend service registry shuts services down in reverse registration order", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        auto order = std::make_shared<std::vector<int>>();
        std::vector<BackendServiceRegistration> registrations;
        for (int index = 1; index <= 3; ++index) {
            auto descriptor = Descriptor();
            descriptor.serviceId.value += std::to_string(index);
            auto registered = services.Register(std::move(descriptor), std::make_unique<OrderedShutdownService>(order, index));
            REQUIRE(registered.HasValue());
            registrations.push_back(std::move(registered).Value());
        }

        REQUIRE(services.BeginShutdown().HasValue());
        CHECK(*order == std::vector<int>{3, 2, 1});
    }

    TEST_CASE("Backend service registry enforces its publication bound", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        std::vector<BackendServiceRegistration> registrations;
        registrations.reserve(BackendServiceRegistry::MaximumServices);
        for (std::size_t index = 0; index < BackendServiceRegistry::MaximumServices; ++index) {
            auto descriptor = Descriptor();
            descriptor.serviceId.value = "com.example.service" + std::to_string(index);
            auto registered = services.Register(std::move(descriptor), std::make_unique<NoopService>());
            REQUIRE(registered.HasValue());
            registrations.push_back(std::move(registered).Value());
        }
        auto overflow = Descriptor();
        overflow.serviceId.value = "com.example.overflow";
        RequireErrorCode(services.Register(std::move(overflow), std::make_unique<NoopService>()), "backend_service_capacity_exceeded");
    }

    TEST_CASE("Backend service defers self-initiated shutdown until its active call exits", "[Extensions][BackendService]") {
        Fixture fixture;
        auto shutdownCount = std::make_shared<std::atomic_int>();
        auto registration = fixture.services.Register(Descriptor(), std::make_unique<SelfStoppingService>(fixture.services, shutdownCount));
        REQUIRE(registration.HasValue());
        auto call = fixture.services.Resolve<SelfStoppingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                  {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(call.HasValue());

        auto stopped = std::move(call).Value().Invoke(&SelfStoppingService::Stop, SumRequest{});
        RequireErrorCode(stopped, "backend_service_cancelled");
        CHECK(shutdownCount->load() == 1);
        CHECK(fixture.services.IsShutdown());
    }

    TEST_CASE("Backend service retains an unresponsive provider and requires restart after its drain deadline",
              "[Extensions][BackendService]") {
        ApplicationCapabilityRegistry capabilities;
        auto capabilityRegistration = capabilities.Register({{"com.example.math.use"}, {1, 0, 0}, "com.example.math-provider", 7});
        REQUIRE(capabilityRegistration.HasValue());
        ExtensionCapabilityAdmission admission = Admission();
        BackendServiceRegistry services({.drainDeadline = std::chrono::milliseconds{1}});
        auto audit = std::make_shared<UnresponsiveService::Audit>();
        auto published = services.Register(Descriptor(), std::make_unique<UnresponsiveService>(audit));
        REQUIRE(published.HasValue());
        BackendServiceRegistration registration = std::move(published).Value();
        auto call =
            services.Resolve<UnresponsiveService>(CapabilityLease(capabilities, admission), {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(call.HasValue());
        auto work = std::async(std::launch::async, [call = std::move(call).Value()]() mutable {
            return std::move(call).Invoke(&UnresponsiveService::IgnoreCancellation, SumRequest{});
        });
        while (!audit->entered.load(std::memory_order_acquire))
            std::this_thread::yield();

        const auto retired = registration.Reset();
        REQUIRE(retired.HasValue());
        CHECK(retired.Value() == BackendServiceRetirementDisposition::RestartRequired);
        CHECK_FALSE(registration.IsRegistered());
        CHECK(audit->shutdownCount.load() == 0);

        audit->release.store(true, std::memory_order_release);
        RequireErrorCode(work.get(), "backend_service_cancelled");
        const auto finalized = services.FinalizeRetiredOnOwnerThread();
        REQUIRE(finalized.HasValue());
        CHECK(finalized.Value() == BackendServiceRetirementDisposition::RestartRequired);
        CHECK(audit->shutdownCount.load() == 0);
    }

    TEST_CASE("Backend service nested re-entrant shutdown finalizes exactly once after the outermost call",
              "[Extensions][BackendService]") {
        Fixture fixture;
        auto control = std::make_shared<NestedCallControl>();
        auto registration = fixture.services.Register(Descriptor(), std::make_unique<NestedSelfStoppingService>(fixture.services, control));
        REQUIRE(registration.HasValue());
        control->invokeInner = [&fixture] {
            auto inner = fixture.services.Resolve<NestedSelfStoppingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                             {"com.example.math"}, {"com.example.math.v1"});
            if (inner.HasError())
                return Result<SumResponse>::Failure(inner.ErrorValue());
            return std::move(inner).Value().Invoke(&NestedSelfStoppingService::Inner, SumRequest{});
        };
        auto outer = fixture.services.Resolve<NestedSelfStoppingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                         {"com.example.math"}, {"com.example.math.v1"});
        REQUIRE(outer.HasValue());

        RequireErrorCode(std::move(outer).Value().Invoke(&NestedSelfStoppingService::Outer, SumRequest{}), "backend_service_cancelled");
        CHECK(control->shutdownCount.load() == 1);
        REQUIRE(fixture.services.BeginShutdown().HasValue());
        CHECK(control->shutdownCount.load() == 1);
    }
}  // namespace Horo::Extensions::Tests
