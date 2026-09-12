#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/ToolchainProviderRegistry.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Horo::Extensions::Tests {
    namespace {
        [[nodiscard]] ToolchainProviderDescriptor Provider(std::string contribution = "toolchain.example",
                                                           const std::uint64_t generation = 7U) {
            return {
                .contributionId = std::move(contribution),
                .providerId = "com.example.toolchain",
                .providerGeneration = generation,
                .tools = {{"tool.compiler"}, {"tool.linker"}},
            };
        }

        class TestPolicy final : public IToolchainInvocationPolicy {
        public:
            TestPolicy() {
                request.executable = "/host/bin/compiler";
                request.arguments = {"--host-approved"};
                request.workingDirectory = "/host/workspace";
                request.environment.base = ProcessEnvironmentBase::Replace;
                request.environment.set.push_back({"SDK_ROOT", "/host/sdk"});
                request.timeout = std::chrono::seconds{2};
                request.gracefulTermination = std::chrono::milliseconds{10};
                request.maximumLineBytes = 123U;
            }

            Result<ExternalProcessRequest> Resolve(const ToolchainProviderDescriptor &provider,
                                                   const ToolchainInvocationIntent &intent) const override {
                ++calls;
                lastProvider = provider;
                lastIntent = intent;
                if (throwException)
                    throw std::runtime_error{"policy failure"};
                if (reject)
                    return Result<ExternalProcessRequest>::Failure(
                        MakeError(ExtensionErrors::InvocationFailed, "intent rejected by test policy"));
                return Result<ExternalProcessRequest>::Success(request);
            }

            mutable std::size_t calls{};
            mutable ToolchainProviderDescriptor lastProvider;
            mutable ToolchainInvocationIntent lastIntent;
            ExternalProcessRequest request;
            bool reject{};
            bool throwException{};
        };

        class TestRunner final : public IExternalProcessRunner {
        public:
            Result<ExternalProcessResult> Run(const ExternalProcessRequest &request, const CancellationToken &cancellation) override {
                ++calls;
                executable = request.executable;
                arguments = request.arguments;
                workingDirectory = request.workingDirectory.string();
                environment = request.environment;
                timeout = request.timeout;
                gracefulTermination = request.gracefulTermination;
                maximumLineBytes = request.maximumLineBytes;
                cancellationObserved = cancellation.IsCancellationRequested();
                if (request.onOutput)
                    request.onOutput({ProcessOutputStream::StandardOutput, "host output", false});
                if (throwException)
                    throw std::runtime_error{"runner failure"};
                if (fail)
                    return Result<ExternalProcessResult>::Failure(MakeError(ExtensionErrors::InvocationFailed, "platform process failure"));
                return Result<ExternalProcessResult>::Success({ProcessTerminationReason::Exited, 0});
            }

            std::size_t calls{};
            std::string executable;
            std::vector<std::string> arguments;
            std::string workingDirectory;
            ProcessEnvironment environment;
            std::chrono::milliseconds timeout{};
            std::chrono::milliseconds gracefulTermination{};
            std::size_t maximumLineBytes{};
            bool cancellationObserved{};
            bool fail{};
            bool throwException{};
        };

        class BlockingRunner final : public IExternalProcessRunner {
        public:
            Result<ExternalProcessResult> Run(const ExternalProcessRequest &, const CancellationToken &cancellation) override {
                {
                    std::scoped_lock lock{mutex_};
                    entered_ = true;
                }
                condition_.notify_all();
                while (!cancellation.IsCancellationRequested())
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                cancelled_.store(true, std::memory_order_release);
                return Result<ExternalProcessResult>::Success({ProcessTerminationReason::Cancelled, 0});
            }

            void WaitUntilEntered() {
                std::unique_lock lock{mutex_};
                condition_.wait(lock, [this] {
                    return entered_;
                });
            }

            [[nodiscard]] bool Cancelled() const noexcept {
                return cancelled_.load(std::memory_order_acquire);
            }

        private:
            std::mutex mutex_;
            std::condition_variable condition_;
            bool entered_{};
            std::atomic_bool cancelled_{};
        };

        template <typename T> void RequireError(const Result<T> &result, const std::string &code) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == "horo.extensions");
            CHECK(result.ErrorValue().code.Value() == code);
        }
    }  // namespace

    TEST_CASE("Toolchain provider invocation is fully resolved by host policy and crosses the platform boundary",
              "[unit][extensions][toolchain-provider][headless]") {
        TestPolicy policy;
        TestRunner runner;
        std::size_t outputCalls = 0U;
        policy.request.onOutput = [&outputCalls](const ProcessOutputLine &line) {
            ++outputCalls;
            CHECK(line.stream == ProcessOutputStream::StandardOutput);
            CHECK(line.text == "host output");
        };
        ToolchainProviderRegistry registry{policy, runner};
        auto registration = registry.Register(Provider());
        REQUIRE(registration.HasValue());

        const ToolchainInvocationIntent intent{{"tool.compiler"}, {"--provider-request", "input.cpp"}};
        const auto invoked = registry.Invoke(registration.Value().Authority(), intent, {});
        REQUIRE(invoked.HasValue());
        CHECK(invoked.Value().provider.contributionId == "toolchain.example");
        CHECK(invoked.Value().provider.providerId == "com.example.toolchain");
        CHECK(invoked.Value().provider.providerGeneration == 7U);
        CHECK(invoked.Value().tool.value == "tool.compiler");
        CHECK(invoked.Value().process.reason == ProcessTerminationReason::Exited);
        CHECK(policy.calls == 1U);
        CHECK(policy.lastIntent.arguments == intent.arguments);
        CHECK(runner.calls == 1U);
        CHECK(runner.executable == "/host/bin/compiler");
        CHECK(runner.arguments == std::vector<std::string>{"--host-approved"});
        CHECK(runner.workingDirectory == "/host/workspace");
        CHECK(runner.environment.base == ProcessEnvironmentBase::Replace);
        REQUIRE(runner.environment.set.size() == 1U);
        CHECK(runner.environment.set.front().name == "SDK_ROOT");
        CHECK(runner.timeout == std::chrono::seconds{2});
        CHECK(runner.gracefulTermination == std::chrono::milliseconds{10});
        CHECK(runner.maximumLineBytes == 123U);
        CHECK(outputCalls == 1U);
    }

    TEST_CASE("Toolchain provider rejects undeclared and malformed intents before host policy",
              "[unit][extensions][toolchain-provider][headless]") {
        TestPolicy policy;
        TestRunner runner;
        ToolchainProviderRegistry registry{policy, runner};
        auto registration = registry.Register(Provider());
        REQUIRE(registration.HasValue());
        const auto authority = registration.Value().Authority();

        RequireError(registry.Invoke(authority, {{"tool.archiver"}, {}}, {}), "toolchain_provider_registry_invalid");
        RequireError(registry.Invoke(authority, {{"Invalid Tool"}, {}}, {}), "toolchain_provider_registry_invalid");
        std::vector<std::string> tooMany(ToolchainProviderRegistry::MaximumArguments + 1U, "argument");
        RequireError(registry.Invoke(authority, {{"tool.compiler"}, std::move(tooMany)}, {}), "toolchain_provider_registry_invalid");
        CHECK(policy.calls == 0U);
        CHECK(runner.calls == 0U);
    }

    TEST_CASE("Toolchain provider contains policy and platform failures with typed causes",
              "[unit][extensions][toolchain-provider][headless]") {
        SECTION("policy rejection") {
            TestPolicy policy;
            policy.reject = true;
            TestRunner runner;
            ToolchainProviderRegistry registry{policy, runner};
            auto registration = registry.Register(Provider());
            REQUIRE(registration.HasValue());
            const auto result = registry.Invoke(registration.Value().Authority(), {{"tool.compiler"}, {}}, {});
            RequireError(result, "toolchain_policy_rejected");
            REQUIRE(result.ErrorValue().cause.Get() != nullptr);
            CHECK(result.ErrorValue().cause.Get()->code.Value() == "invocation_failed");
            CHECK(runner.calls == 0U);
        }

        SECTION("platform failure") {
            TestPolicy policy;
            TestRunner runner;
            runner.fail = true;
            ToolchainProviderRegistry registry{policy, runner};
            auto registration = registry.Register(Provider());
            REQUIRE(registration.HasValue());
            const auto result = registry.Invoke(registration.Value().Authority(), {{"tool.compiler"}, {}}, {});
            RequireError(result, "toolchain_invocation_failed");
            REQUIRE(result.ErrorValue().cause.Get() != nullptr);
            CHECK(result.ErrorValue().cause.Get()->code.Value() == "invocation_failed");
        }

        SECTION("policy exception") {
            TestPolicy policy;
            policy.throwException = true;
            TestRunner runner;
            ToolchainProviderRegistry registry{policy, runner};
            auto registration = registry.Register(Provider());
            REQUIRE(registration.HasValue());
            RequireError(registry.Invoke(registration.Value().Authority(), {{"tool.compiler"}, {}}, {}), "toolchain_policy_rejected");
            CHECK(runner.calls == 0U);
        }

        SECTION("platform exception") {
            TestPolicy policy;
            TestRunner runner;
            runner.throwException = true;
            ToolchainProviderRegistry registry{policy, runner};
            auto registration = registry.Register(Provider());
            REQUIRE(registration.HasValue());
            const auto result = registry.Invoke(registration.Value().Authority(), {{"tool.compiler"}, {}}, {});
            RequireError(result, "toolchain_invocation_failed");
            REQUIRE(result.ErrorValue().cause.Get() != nullptr);
            CHECK(result.ErrorValue().cause.Get()->code.Value() == "invocation_failed");
        }

        SECTION("invalid host resolution") {
            TestPolicy policy;
            policy.request.executable = "  ";
            TestRunner runner;
            ToolchainProviderRegistry registry{policy, runner};
            auto registration = registry.Register(Provider());
            REQUIRE(registration.HasValue());
            RequireError(registry.Invoke(registration.Value().Authority(), {{"tool.compiler"}, {}}, {}),
                         "toolchain_provider_registry_invalid");
            CHECK(runner.calls == 0U);
        }
    }

    TEST_CASE("Toolchain provider revocation cancels admitted work and rejects stale authorities",
              "[unit][extensions][toolchain-provider][headless]") {
        TestPolicy policy;
        BlockingRunner runner;
        ToolchainProviderRegistry registry{policy, runner};
        auto registered = registry.Register(Provider());
        REQUIRE(registered.HasValue());
        ToolchainProviderRegistration registration = std::move(registered).Value();
        const auto authority = registration.Authority();
        auto invocation = std::async(std::launch::async, [&] {
            return registry.Invoke(authority, {{"tool.compiler"}, {}}, {});
        });
        runner.WaitUntilEntered();

        registration.Reset();
        const auto completed = invocation.get();
        REQUIRE(completed.HasValue());
        CHECK(completed.Value().process.reason == ProcessTerminationReason::Cancelled);
        CHECK(runner.Cancelled());
        RequireError(registry.Invoke(authority, {{"tool.compiler"}, {}}, {}), "toolchain_provider_unavailable");
    }

    TEST_CASE("Toolchain provider validates publication identity capacity and shutdown",
              "[unit][extensions][toolchain-provider][headless]") {
        TestPolicy policy;
        TestRunner runner;
        ToolchainProviderRegistry registry{policy, runner};
        auto malformed = Provider("Invalid Provider");
        RequireError(registry.Register(std::move(malformed)), "toolchain_provider_registry_invalid");
        auto noGeneration = Provider("toolchain.zero", 0U);
        RequireError(registry.Register(std::move(noGeneration)), "toolchain_provider_registry_invalid");
        auto unsorted = Provider("toolchain.unsorted");
        unsorted.tools = {{"tool.z"}, {"tool.a"}};
        RequireError(registry.Register(std::move(unsorted)), "toolchain_provider_registry_invalid");

        auto first = registry.Register(Provider());
        REQUIRE(first.HasValue());
        CHECK(first.Value().IsRegistered());
        RequireError(registry.Register(Provider("toolchain.example", 8U)), "toolchain_provider_registry_duplicate");
        const ToolchainInvocationAuthority wrongGeneration{"toolchain.example", 8U};
        RequireError(registry.Invoke(wrongGeneration, {{"tool.compiler"}, {}}, {}), "toolchain_provider_unavailable");

        registry.BeginShutdown();
        CHECK(registry.IsShutdown());
        CHECK_FALSE(first.Value().IsRegistered());
        RequireError(registry.Register(Provider("toolchain.after-shutdown")), "toolchain_provider_registry_shutdown");
        RequireError(registry.Invoke(first.Value().Authority(), {{"tool.compiler"}, {}}, {}), "toolchain_provider_registry_shutdown");
    }

    TEST_CASE("Toolchain provider registry enforces its hard publication bound", "[unit][extensions][toolchain-provider][headless]") {
        TestPolicy policy;
        TestRunner runner;
        ToolchainProviderRegistry registry{policy, runner};
        std::vector<ToolchainProviderRegistration> registrations;
        registrations.reserve(ToolchainProviderRegistry::MaximumProviders);
        for (std::size_t index = 0; index < ToolchainProviderRegistry::MaximumProviders; ++index) {
            auto registered = registry.Register(Provider("toolchain.capacity-" + std::to_string(index)));
            REQUIRE(registered.HasValue());
            registrations.push_back(std::move(registered).Value());
        }
        RequireError(registry.Register(Provider("toolchain.overflow")), "toolchain_provider_registry_capacity_exceeded");
    }
}  // namespace Horo::Extensions::Tests
