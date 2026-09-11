#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ProjectValidatorRegistry.h"
#include "Horo/Foundation/ModuleDescriptor.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Extensions::Tests {
    namespace {
        const ErrorCodeDescriptor Finding{.domain = ErrorDomainId{"horo.test.validator"},
                                          .code = ErrorCode{"project.finding"},
                                          .defaultSeverity = ErrorSeverity::Warning,
                                          .summary = "Project finding."};

        [[nodiscard]] ErrorCodeRegistry ErrorRegistry() {
            ModuleDescriptor module{.id = ModuleId{"horo.test-validator"}, .version = {1, 0, 0}};
            module.errorDomains.push_back({.id = ErrorDomainId{"horo.test.validator"}, .descriptors = {&Finding}});
            auto registry = BuildErrorCodeRegistry(std::span{&module, 1U});
            REQUIRE(registry.HasValue());
            return std::move(registry).Value();
        }

        [[nodiscard]] ProjectValidatorRegistry Registry(const ValidationResultLimits limits = {}) {
            auto registry = ProjectValidatorRegistry::Create(ErrorRegistry(), limits);
            REQUIRE(registry.HasValue());
            return std::move(registry).Value();
        }

        [[nodiscard]] ProjectPath Path(const std::string_view path) {
            auto parsed = ProjectPath::Parse(path);
            REQUIRE(parsed.HasValue());
            return std::move(parsed).Value();
        }

        struct SnapshotFixture final {
            std::array<std::byte, 2> projectBytes{};
            std::array<std::byte, 1> sceneBytes{};
            std::array<ProjectValidationResourceView, 2> resources{ProjectValidationResourceView{Path(".horo/project.json"), projectBytes},
                                                                   ProjectValidationResourceView{Path("assets/scenes/main.horo"),
                                                                                                 sceneBytes}};

            [[nodiscard]] ProjectValidationSnapshot Snapshot() const {
                return {.projectId = "project-123", .mode = ProjectValidationMode::ReadOnly, .resources = resources};
            }
        };

        class RecordingValidator final : public IProjectValidator {
        public:
            explicit RecordingValidator(std::string message, const bool throwException = false)
                : message_(std::move(message)), throwException_(throwException) {}

            Result<void> Validate(const ProjectValidationSnapshot &snapshot, ProjectValidationFindingSink &findings,
                                  const CancellationToken &) const override {
                ++calls;
                observedProjectId = snapshot.projectId;
                observedPath = snapshot.resources.front().path.String();
                if (throwException_)
                    throw std::runtime_error{"provider failure"};
                return findings.Add(Finding, message_, {.source = snapshot.resources.back().path.String(), .line = 3U, .column = 1U});
            }

            mutable std::atomic_size_t calls{};
            mutable std::string observedProjectId;
            mutable std::string observedPath;

        private:
            std::string message_;
            bool throwException_{};
        };

        class CancellingValidator final : public IProjectValidator {
        public:
            explicit CancellingValidator(CancellationSource cancellation) : cancellation_(std::move(cancellation)) {}

            Result<void> Validate(const ProjectValidationSnapshot &, ProjectValidationFindingSink &findings,
                                  const CancellationToken &) const override {
                const auto added = findings.Add(Finding, "must not publish", {.source = ".horo/project.json"});
                cancellation_.RequestCancellation();
                return added;
            }

        private:
            CancellationSource cancellation_;
        };

        class BlockingValidator final : public IProjectValidator {
        public:
            Result<void> Validate(const ProjectValidationSnapshot &, ProjectValidationFindingSink &,
                                  const CancellationToken &) const override {
                std::unique_lock lock{mutex_};
                entered_ = true;
                condition_.notify_all();
                condition_.wait(lock, [this] {
                    return released_;
                });
                return Result<void>::Success();
            }

            void WaitUntilEntered() const {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [this] {
                    return entered_;
                });
            }

            void Release() const {
                std::scoped_lock lock{mutex_};
                released_ = true;
                condition_.notify_all();
            }

        private:
            mutable std::mutex mutex_;
            mutable std::condition_variable condition_;
            mutable bool entered_{};
            mutable bool released_{};
        };

        [[nodiscard]] ProjectValidatorProviderDescriptor Provider(std::string validatorId, const std::uint64_t generation = 1U) {
            return {.validatorId = {std::move(validatorId)},
                    .providerId = {"com.example.project-validator"},
                    .providerGeneration = generation};
        }

        void RequireError(const auto &result, const std::string_view code) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == "horo.extensions");
            CHECK(result.ErrorValue().code.Value() == code);
        }
    }  // namespace

    TEST_CASE("Project validators run in deterministic identity order with exact attribution",
              "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        auto secondProvider = std::make_shared<RecordingValidator>("second");
        auto firstProvider = std::make_shared<RecordingValidator>("first");
        auto second = registry.Register(Provider("validator.z", 7U), secondProvider);
        auto first = registry.Register(Provider("validator.a", 4U), firstProvider);
        REQUIRE(second.HasValue());
        REQUIRE(first.HasValue());
        CHECK(secondProvider->calls == 0U);
        CHECK(firstProvider->calls == 0U);

        SnapshotFixture fixture;
        const auto results = registry.ValidateAll(fixture.Snapshot(), {});
        REQUIRE(results.HasValue());
        REQUIRE(results.Value().size() == 2U);
        CHECK(results.Value()[0].provider.validatorId.value == "validator.a");
        CHECK(results.Value()[0].provider.providerGeneration == 4U);
        CHECK(results.Value()[0].result.Diagnostics().front().message == "first");
        CHECK(results.Value()[1].provider.validatorId.value == "validator.z");
        CHECK(results.Value()[1].provider.providerGeneration == 7U);
        CHECK(firstProvider->observedProjectId == "project-123");
        CHECK(firstProvider->observedPath == ".horo/project.json");
    }

    TEST_CASE("Project validator cancellation never publishes partial attributed results",
              "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        CancellationSource beforeCall;
        auto neverCalled = std::make_shared<RecordingValidator>("unreachable");
        auto registration = registry.Register(Provider("validator.cancelled"), neverCalled);
        REQUIRE(registration.HasValue());
        ProjectValidatorRegistration ownedRegistration = std::move(registration).Value();
        beforeCall.RequestCancellation();
        SnapshotFixture fixture;
        RequireError(registry.ValidateAll(fixture.Snapshot(), beforeCall.Token()), "project_validation_cancelled");
        CHECK(neverCalled->calls == 0U);

        ownedRegistration.Reset();
        CancellationSource duringCall;
        auto cancelling = registry.Register(Provider("validator.cancels"), std::make_shared<CancellingValidator>(duringCall));
        REQUIRE(cancelling.HasValue());
        RequireError(registry.ValidateAll(fixture.Snapshot(), duringCall.Token()), "project_validation_cancelled");
    }

    TEST_CASE("Project validator failures are contained and attributed without partial results",
              "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        auto throwing = registry.Register(Provider("validator.throws", 9U), std::make_shared<RecordingValidator>("", true));
        REQUIRE(throwing.HasValue());
        SnapshotFixture fixture;
        const auto result = registry.ValidateAll(fixture.Snapshot(), {});
        RequireError(result, "project_validator_invocation_failed");
        CHECK(result.ErrorValue().message.find("com.example.project-validator@9") != std::string::npos);
    }

    TEST_CASE("Project validator registration rejects malformed duplicate and shutdown publication",
              "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        auto provider = std::make_shared<RecordingValidator>("finding");
        auto malformed = Provider("Invalid Validator");
        RequireError(registry.Register(std::move(malformed), provider), "project_validator_registry_invalid");
        auto malformedProvider = Provider("validator.valid");
        malformedProvider.providerId.value = "Invalid Provider";
        RequireError(registry.Register(std::move(malformedProvider), provider), "project_validator_registry_invalid");
        RequireError(registry.Register(Provider("validator.zero-generation", 0U), provider), "project_validator_registry_invalid");
        RequireError(registry.Register(Provider("validator.null"), {}), "project_validator_registry_invalid");

        auto first = registry.Register(Provider("validator.identity"), provider);
        REQUIRE(first.HasValue());
        RequireError(registry.Register(Provider("validator.identity", 2U), provider), "project_validator_registry_duplicate");
        registry.BeginShutdown();
        CHECK(registry.IsShutdown());
        CHECK_FALSE(first.Value().IsRegistered());
        RequireError(registry.Register(Provider("validator.other"), provider), "project_validator_registry_shutdown");
    }

    TEST_CASE("Project validator registration enforces its hard provider bound", "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        auto provider = std::make_shared<RecordingValidator>("finding");
        std::vector<ProjectValidatorRegistration> registrations;
        registrations.reserve(ProjectValidatorRegistry::MaximumProviders);
        for (std::size_t index = 0; index < ProjectValidatorRegistry::MaximumProviders; ++index) {
            auto registered = registry.Register(Provider("validator.capacity-" + std::to_string(index)), provider);
            REQUIRE(registered.HasValue());
            registrations.push_back(std::move(registered).Value());
        }
        RequireError(registry.Register(Provider("validator.capacity-overflow"), provider), "project_validator_registry_capacity_exceeded");
    }

    TEST_CASE("Project validator snapshots enforce normalized project-relative bounds", "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        SnapshotFixture fixture;
        std::array reversed{fixture.resources[1], fixture.resources[0]};
        auto snapshot = fixture.Snapshot();
        snapshot.resources = reversed;
        RequireError(registry.ValidateAll(snapshot, {}), "project_validator_registry_invalid");
        snapshot = fixture.Snapshot();
        snapshot.projectId = "   ";
        RequireError(registry.ValidateAll(snapshot, {}), "project_validator_registry_invalid");
        snapshot = fixture.Snapshot();
        const std::string oversizedProjectId(ProjectValidatorRegistry::MaximumProjectIdBytes + 1U, 'p');
        snapshot.projectId = oversizedProjectId;
        RequireError(registry.ValidateAll(snapshot, {}), "project_validator_registry_invalid");
        snapshot = fixture.Snapshot();
        snapshot.mode = static_cast<ProjectValidationMode>(255U);
        RequireError(registry.ValidateAll(snapshot, {}), "project_validator_registry_invalid");
        std::vector<ProjectValidationResourceView> oversizedResources(ProjectValidatorRegistry::MaximumResources + 1U,
                                                                      fixture.resources.front());
        snapshot = fixture.Snapshot();
        snapshot.resources = oversizedResources;
        RequireError(registry.ValidateAll(snapshot, {}), "project_validator_registry_invalid");

        RequireError(ProjectValidatorRegistry::Create(ErrorRegistry(), {.maximumDiagnostics = 0U}), "project_validator_registry_invalid");
    }

    TEST_CASE("Project validator findings reject roots and preserve normalized source detail",
              "[unit][extensions][project-validator][headless]") {
        class AbsoluteFindingValidator final : public IProjectValidator {
        public:
            Result<void> Validate(const ProjectValidationSnapshot &, ProjectValidationFindingSink &findings,
                                  const CancellationToken &) const override {
                static_cast<void>(findings.Add(Finding, {}, {.source = "/tmp/project.json"}));
                return Result<void>::Success();
            }
        };

        ProjectValidatorRegistry registry = Registry();
        SnapshotFixture fixture;
        auto absolute = registry.Register(Provider("validator.absolute"), std::make_shared<AbsoluteFindingValidator>());
        REQUIRE(absolute.HasValue());
        RequireError(registry.ValidateAll(fixture.Snapshot(), {}), "project_validator_invocation_failed");

        class NormalizingFindingValidator final : public IProjectValidator {
        public:
            Result<void> Validate(const ProjectValidationSnapshot &, ProjectValidationFindingSink &findings,
                                  const CancellationToken &) const override {
                return findings.Add(Finding, {}, {.source = ".horo//project.json", .line = 7U, .column = 2U});
            }
        };

        ProjectValidatorRegistration absoluteRegistration = std::move(absolute).Value();
        absoluteRegistration.Reset();
        auto normalizing = registry.Register(Provider("validator.normalizing"), std::make_shared<NormalizingFindingValidator>());
        REQUIRE(normalizing.HasValue());
        const auto normalized = registry.ValidateAll(fixture.Snapshot(), {});
        REQUIRE(normalized.HasValue());
        REQUIRE(normalized.Value().size() == 1U);
        REQUIRE(normalized.Value().front().result.Diagnostics().size() == 1U);
        CHECK(normalized.Value().front().result.Diagnostics().front().location.source == ".horo/project.json");
        CHECK(normalized.Value().front().result.Diagnostics().front().location.line == 7U);
        CHECK(normalized.Value().front().result.Diagnostics().front().location.column == 2U);
    }

    TEST_CASE("Unregister and shutdown retain an admitted in-flight project validator", "[unit][extensions][project-validator][headless]") {
        ProjectValidatorRegistry registry = Registry();
        auto blocking = std::make_shared<BlockingValidator>();
        std::weak_ptr<const IProjectValidator> lifetime = blocking;
        auto registration = registry.Register(Provider("validator.blocking"), blocking);
        REQUIRE(registration.HasValue());
        ProjectValidatorRegistration ownedRegistration = std::move(registration).Value();
        SnapshotFixture fixture;
        auto future = std::async(std::launch::async, [&] {
            return registry.ValidateAll(fixture.Snapshot(), {});
        });
        blocking->WaitUntilEntered();

        blocking.reset();
        ownedRegistration.Reset();
        registry.BeginShutdown();
        CHECK_FALSE(lifetime.expired());
        auto retained = lifetime.lock();
        REQUIRE(retained != nullptr);
        std::static_pointer_cast<const BlockingValidator>(retained)->Release();
        retained.reset();

        const auto result = future.get();
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().size() == 1U);
        CHECK(lifetime.expired());
    }
}  // namespace Horo::Extensions::Tests
