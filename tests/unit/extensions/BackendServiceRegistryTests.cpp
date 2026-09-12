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
                audit_->entered.notify_all();
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

        class BlockingShutdownService final {
        public:
            struct Audit final {
                std::atomic_bool entered{};
                std::atomic_bool release{};
                std::atomic_int count{};
            };

            explicit BlockingShutdownService(std::shared_ptr<Audit> audit) noexcept : audit_(std::move(audit)) {}

            void Shutdown() noexcept {
                audit_->entered.store(true, std::memory_order_release);
                audit_->entered.notify_all();
                audit_->release.wait(false, std::memory_order_acquire);
                ++audit_->count;
            }

        private:
            std::shared_ptr<Audit> audit_;
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
                audit_->entered.notify_all();
                audit_->release.wait(false, std::memory_order_acquire);
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
            std::shared_ptr<std::vector<int>> shutdownOrder;
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
                control_->shutdownOrder->push_back(2);
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

        [[nodiscard]] BackendServiceCodeLease TestCodeLease() {
            return BackendServiceCodeLease::Retain(std::make_shared<int>());
        }

        template <typename Service>
        [[nodiscard]] BackendServiceRegistration RegisterService(BackendServiceRegistry &registry, BackendServiceDescriptor descriptor,
                                                                 std::unique_ptr<Service> service) {
            auto registered = registry.Register(std::move(descriptor), std::move(service), TestCodeLease());
            REQUIRE(registered.HasValue());
            return std::move(registered).Value();
        }

        template <typename Service>
        [[nodiscard]] BackendServiceCall<Service> ResolveService(BackendServiceRegistry &services,
                                                                 ApplicationCapabilityRegistry &capabilities,
                                                                 ExtensionCapabilityAdmission &admission) {
            auto call = services.Resolve<Service>(CapabilityLease(capabilities, admission), {"com.example.math"}, {"com.example.math.v1"});
            REQUIRE(call.HasValue());
            return std::move(call).Value();
        }

        template <typename Service, typename Operation>
        [[nodiscard]] std::future<Result<SumResponse>> StartAsyncCall(BackendServiceRegistry &services,
                                                                      ApplicationCapabilityRegistry &capabilities,
                                                                      ExtensionCapabilityAdmission &admission, Operation operation) {
            auto call = ResolveService<Service>(services, capabilities, admission);
            return std::async(std::launch::async, [call = std::move(call), operation]() mutable {
                return std::move(call).Invoke(operation, SumRequest{});
            });
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
        auto registration = RegisterService(fixture.services, Descriptor(), std::make_unique<ArithmeticService>(audit));
        auto call = ResolveService<ArithmeticService>(fixture.services, fixture.capabilities, fixture.admission);
        auto response = std::move(call).Invoke(&ArithmeticService::Add, SumRequest{20, 22});
        REQUIRE(response.HasValue());
        CHECK(response.Value().value == 42);
        CHECK(audit->observedProvider == "com.example.math-provider");
    }

    TEST_CASE("Backend service preserves typed provider errors and caller cancellation", "[Extensions][BackendService]") {
        Fixture fixture;
        auto registration = RegisterService(fixture.services, Descriptor(),
                                            std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()));
        auto failure = ResolveService<ArithmeticService>(fixture.services, fixture.capabilities, fixture.admission);
        auto failed = std::move(failure).Invoke(&ArithmeticService::Fail, SumRequest{});
        RequireErrorCode(failed, "backend_service_invocation_failed");
        REQUIRE(failed.ErrorValue().cause);
        CHECK(failed.ErrorValue().cause.Get()->code.Value() == "contribution_rejected");

        CancellationSource cancellation;
        cancellation.RequestCancellation();
        auto cancelled = ResolveService<ArithmeticService>(fixture.services, fixture.capabilities, fixture.admission);
        RequireErrorCode(std::move(cancelled).Invoke(&ArithmeticService::Add, SumRequest{}, cancellation.Token()),
                         "backend_service_cancelled");
    }

    TEST_CASE("Backend service rejects mismatched contracts types and threads before provider invocation", "[Extensions][BackendService]") {
        Fixture fixture;
        auto audit = std::make_shared<ArithmeticServiceAudit>();
        auto registration = RegisterService(fixture.services, Descriptor(BackendServiceThreadRule::ProviderOwnerThread),
                                            std::make_unique<ArithmeticService>(audit));
        RequireErrorCode(fixture.services.Resolve<ArithmeticService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                     {"com.example.math"}, {"com.example.other.v1"}),
                         "backend_service_contract_mismatch");
        RequireErrorCode(fixture.services.Resolve<NoopService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                               {"com.example.math"}, {"com.example.math.v1"}),
                         "backend_service_type_mismatch");
        auto call = ResolveService<ArithmeticService>(fixture.services, fixture.capabilities, fixture.admission);
        auto wrongThread = std::async(std::launch::async, [call = std::move(call)]() mutable {
            return std::move(call).Invoke(&ArithmeticService::Add, SumRequest{});
        });
        RequireErrorCode(wrongThread.get(), "backend_service_thread_violation");
        CHECK(audit->observedProvider.empty());
    }

    TEST_CASE("Backend service revocation cancels and drains work before shutdown", "[Extensions][BackendService]") {
        Fixture fixture;
        auto audit = std::make_shared<DrainingService::Audit>();
        BackendServiceRegistration registration = RegisterService(fixture.services, Descriptor(), std::make_unique<DrainingService>(audit));
        auto work = StartAsyncCall<DrainingService>(fixture.services, fixture.capabilities, fixture.admission,
                                                    &DrainingService::WaitForCancellation);
        audit->entered.wait(false, std::memory_order_acquire);
        CHECK(registration.Reset() == BackendServiceRetirementDisposition::ShutdownComplete);
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
        BackendServiceRegistration registration =
            RegisterService(fixture.services, Descriptor(BackendServiceThreadRule::ProviderOwnerThread),
                            std::make_unique<OwnerThreadShutdownService>(audit));

        auto wrongThread = std::async(std::launch::async, [&registration] {
            return registration.Reset();
        });
        const auto retired = wrongThread.get();
        CHECK(retired == BackendServiceRetirementDisposition::OwnerThreadFinalizationRequired);
        CHECK_FALSE(registration.IsRegistered());
        CHECK(audit->shutdownCount.load() == 0);

        const auto finalized = fixture.services.FinalizeRetiredOnOwnerThread();
        CHECK(finalized == BackendServiceRetirementDisposition::ShutdownComplete);
        CHECK(audit->shutdownCount.load() == 1);
        CHECK(audit->shutdownThread == ownerThread);
    }

    TEST_CASE("Backend service registry validates publication and shuts down terminally", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        auto invalid = Descriptor();
        invalid.providerGeneration = 0;
        RequireErrorCode(services.Register(std::move(invalid),
                                           std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()),
                                           TestCodeLease()),
                         "backend_service_invalid");
        RequireErrorCode(services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()),
                                           BackendServiceCodeLease::Retain(std::shared_ptr<int>{})),
                         "backend_service_invalid");
        auto audit = std::make_shared<ArithmeticServiceAudit>();
        auto registration = RegisterService(services, Descriptor(), std::make_unique<ArithmeticService>(audit));
        RequireErrorCode(services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()),
                                           TestCodeLease()),
                         "backend_service_duplicate");
        CHECK(services.BeginShutdown() == BackendServiceRetirementDisposition::ShutdownComplete);
        CHECK(services.IsShutdown());
        CHECK(audit->shutdownCount.load() == 1);
        CHECK_FALSE(registration.IsRegistered());
        RequireErrorCode(services.Register(Descriptor(), std::make_unique<ArithmeticService>(std::make_shared<ArithmeticServiceAudit>()),
                                           TestCodeLease()),
                         "backend_service_shutdown");
    }

    TEST_CASE("Backend service registry shuts services down in reverse registration order", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        auto order = std::make_shared<std::vector<int>>();
        std::vector<BackendServiceRegistration> registrations;
        for (int index = 1; index <= 3; ++index) {
            auto descriptor = Descriptor();
            descriptor.serviceId.value += std::to_string(index);
            registrations.push_back(
                RegisterService(services, std::move(descriptor), std::make_unique<OrderedShutdownService>(order, index)));
        }

        CHECK(services.BeginShutdown() == BackendServiceRetirementDisposition::ShutdownComplete);
        CHECK(*order == std::vector<int>{3, 2, 1});
    }

    TEST_CASE("Backend service registry enforces its publication bound", "[Extensions][BackendService]") {
        BackendServiceRegistry services;
        std::vector<BackendServiceRegistration> registrations;
        registrations.reserve(BackendServiceRegistry::MaximumServices);
        for (std::size_t index = 0; index < BackendServiceRegistry::MaximumServices; ++index) {
            auto descriptor = Descriptor();
            descriptor.serviceId.value = "com.example.service" + std::to_string(index);
            registrations.push_back(RegisterService(services, std::move(descriptor), std::make_unique<NoopService>()));
        }
        auto overflow = Descriptor();
        overflow.serviceId.value = "com.example.overflow";
        RequireErrorCode(services.Register(std::move(overflow), std::make_unique<NoopService>(), TestCodeLease()),
                         "backend_service_capacity_exceeded");
    }

    TEST_CASE("Backend service defers self-initiated shutdown until its active call exits", "[Extensions][BackendService]") {
        Fixture fixture;
        auto control = std::make_shared<NestedCallControl>();
        control->shutdownOrder = std::make_shared<std::vector<int>>();
        auto registration =
            RegisterService(fixture.services, Descriptor(), std::make_unique<NestedSelfStoppingService>(fixture.services, control));
        auto call = ResolveService<NestedSelfStoppingService>(fixture.services, fixture.capabilities, fixture.admission);

        auto stopped = std::move(call).Invoke(&NestedSelfStoppingService::Inner, SumRequest{});
        RequireErrorCode(stopped, "backend_service_cancelled");
        CHECK(control->shutdownCount.load() == 1);
        CHECK(fixture.services.IsShutdown());
    }

    TEST_CASE("Backend service retains an unresponsive provider and requires restart after its drain deadline",
              "[Extensions][BackendService]") {
        std::weak_ptr<int> codeLifetime;
        {
            ApplicationCapabilityRegistry capabilities;
            auto capabilityRegistration = capabilities.Register({{"com.example.math.use"}, {1, 0, 0}, "com.example.math-provider", 7});
            REQUIRE(capabilityRegistration.HasValue());
            ExtensionCapabilityAdmission admission = Admission();
            BackendServiceRegistry services({.drainDeadline = std::chrono::milliseconds{1}});
            auto audit = std::make_shared<UnresponsiveService::Audit>();
            auto codeOwner = std::make_shared<int>();
            codeLifetime = codeOwner;
            auto published =
                services.Register(Descriptor(), std::make_unique<UnresponsiveService>(audit), BackendServiceCodeLease::Retain(codeOwner));
            REQUIRE(published.HasValue());
            BackendServiceRegistration registration = std::move(published).Value();
            codeOwner.reset();
            auto work = StartAsyncCall<UnresponsiveService>(services, capabilities, admission, &UnresponsiveService::IgnoreCancellation);
            audit->entered.wait(false, std::memory_order_acquire);

            const auto retired = registration.Reset();
            CHECK(retired == BackendServiceRetirementDisposition::RestartRequired);
            CHECK_FALSE(registration.IsRegistered());
            CHECK(audit->shutdownCount.load() == 0);

            audit->release.store(true, std::memory_order_release);
            audit->release.notify_all();
            RequireErrorCode(work.get(), "backend_service_cancelled");
            const auto finalized = services.FinalizeRetiredOnOwnerThread();
            CHECK(finalized == BackendServiceRetirementDisposition::RestartRequired);
            CHECK(audit->shutdownCount.load() == 0);
            CHECK_FALSE(codeLifetime.expired());
        }
        CHECK_FALSE(codeLifetime.expired());
    }

    TEST_CASE("Backend service finalization is not complete before Shutdown returns", "[Extensions][BackendService]") {
        BackendServiceRegistry services({.drainDeadline = std::chrono::milliseconds{1}});
        auto audit = std::make_shared<BlockingShutdownService::Audit>();
        BackendServiceRegistration registration = RegisterService(services, Descriptor(), std::make_unique<BlockingShutdownService>(audit));
        auto firstRetirement = std::async(std::launch::async, [&registration] {
            return registration.Reset();
        });
        audit->entered.wait(false, std::memory_order_acquire);

        const auto concurrentRetirement = services.BeginShutdown();
        CHECK(concurrentRetirement == BackendServiceRetirementDisposition::RestartRequired);
        CHECK(audit->count.load() == 0);

        audit->release.store(true, std::memory_order_release);
        audit->release.notify_all();
        const auto completedCallback = firstRetirement.get();
        CHECK(completedCallback == BackendServiceRetirementDisposition::RestartRequired);
        CHECK(audit->count.load() == 1);
    }

    TEST_CASE("Backend service nested re-entrant shutdown finalizes exactly once after the outermost call",
              "[Extensions][BackendService]") {
        Fixture fixture;
        auto control = std::make_shared<NestedCallControl>();
        auto shutdownOrder = std::make_shared<std::vector<int>>();
        control->shutdownOrder = shutdownOrder;
        auto earlierDescriptor = Descriptor();
        earlierDescriptor.serviceId.value = "com.example.earlier";
        auto earlier =
            RegisterService(fixture.services, std::move(earlierDescriptor), std::make_unique<OrderedShutdownService>(shutdownOrder, 1));
        auto registration =
            RegisterService(fixture.services, Descriptor(), std::make_unique<NestedSelfStoppingService>(fixture.services, control));
        control->invokeInner = [&fixture] {
            auto inner = fixture.services.Resolve<NestedSelfStoppingService>(CapabilityLease(fixture.capabilities, fixture.admission),
                                                                             {"com.example.math"}, {"com.example.math.v1"});
            if (inner.HasError())
                return Result<SumResponse>::Failure(inner.ErrorValue());
            return std::move(inner).Value().Invoke(&NestedSelfStoppingService::Inner, SumRequest{});
        };
        auto outer = ResolveService<NestedSelfStoppingService>(fixture.services, fixture.capabilities, fixture.admission);

        RequireErrorCode(std::move(outer).Invoke(&NestedSelfStoppingService::Outer, SumRequest{}), "backend_service_cancelled");
        CHECK(control->shutdownCount.load() == 1);
        CHECK(*shutdownOrder == std::vector<int>{2, 1});
        CHECK(fixture.services.BeginShutdown() == BackendServiceRetirementDisposition::ShutdownComplete);
        CHECK(control->shutdownCount.load() == 1);
    }
}  // namespace Horo::Extensions::Tests
