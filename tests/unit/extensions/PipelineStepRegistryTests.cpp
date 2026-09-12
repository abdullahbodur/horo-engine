#include "Horo/Extensions/ExtensionErrors.h"
#include "Horo/Extensions/PipelineStepRegistry.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Extensions::Tests {
    namespace {
        using StepBody = std::function<Result<void>(const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &)>;

        class TestStep final : public IPipelineStep {
        public:
            explicit TestStep(StepBody body) : body_(std::move(body)) {}

            Result<void> Execute(const PipelineStepContext &context, PipelineOutputSink &outputs,
                                 const CancellationToken &cancellation) const override {
                ++calls;
                return body_(context, outputs, cancellation);
            }

            mutable std::size_t calls{};

        private:
            StepBody body_;
        };

        [[nodiscard]] PipelineStepDescriptor Descriptor(std::string id, std::vector<PipelineArtifactId> inputs,
                                                        std::vector<PipelineArtifactId> outputs,
                                                        std::vector<PipelineStepId> dependencies = {},
                                                        const PipelinePhase phase = PipelinePhase::Build,
                                                        const std::uint64_t generation = 1U) {
            return {
                .stepId = {std::move(id)},
                .providerId = "com.example.pipeline",
                .providerGeneration = generation,
                .phase = phase,
                .dependencies = std::move(dependencies),
                .inputs = std::move(inputs),
                .outputs = std::move(outputs),
            };
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::initializer_list<unsigned char> values) {
            std::vector<std::byte> result;
            result.reserve(values.size());
            for (const unsigned char value : values)
                result.push_back(static_cast<std::byte>(value));
            return result;
        }

        template <typename T> void RequireError(const Result<T> &result, const std::string &code) {
            REQUIRE(result.HasError());
            CHECK(result.ErrorValue().domain.Value() == "horo.extensions");
            CHECK(result.ErrorValue().code.Value() == code);
        }

        [[nodiscard]] const PipelineArtifact *FindOutput(const PipelineRunResult &result, const std::string &id) {
            for (const PipelineArtifact &artifact : result.outputs) {
                if (artifact.id.value == id)
                    return &artifact;
            }
            return nullptr;
        }
    }  // namespace

    TEST_CASE("Pipeline steps execute in deterministic dependency order and publish complete output",
              "[unit][extensions][pipeline-step][headless]") {
        PipelineStepRegistry registry;
        const auto source = Bytes({1U, 2U});
        const std::array initial{PipelineArtifactView{"artifact.source", source}};

        auto package =
            std::make_shared<TestStep>([](const PipelineStepContext &context, PipelineOutputSink &outputs, const CancellationToken &) {
            const auto *compiled = context.Find({"artifact.compiled"});
            REQUIRE(compiled != nullptr);
            CHECK(compiled->bytes.size() == 3U);
            const auto packageBytes = Bytes({9U});
            return outputs.Write({"artifact.package"}, packageBytes);
        });
        auto compile =
            std::make_shared<TestStep>([](const PipelineStepContext &context, PipelineOutputSink &outputs, const CancellationToken &) {
            const auto *input = context.Find({"artifact.source"});
            REQUIRE(input != nullptr);
            CHECK(input->bytes.size() == 2U);
            CHECK(context.Find({"artifact.package"}) == nullptr);
            const auto compiled = Bytes({3U, 4U, 5U});
            return outputs.Write({"artifact.compiled"}, compiled);
        });

        auto packageRegistration = registry.Register(Descriptor("step.package", {{"artifact.compiled"}}, {{"artifact.package"}},
                                                                {{"step.compile"}}, PipelinePhase::Package),
                                                     package);
        auto compileRegistration =
            registry.Register(Descriptor("step.compile", {{"artifact.source"}}, {{"artifact.compiled"}}, {}, PipelinePhase::Build),
                              compile);
        REQUIRE(packageRegistration.HasValue());
        REQUIRE(compileRegistration.HasValue());

        const auto executed = registry.Execute(initial, {});
        REQUIRE(executed.HasValue());
        REQUIRE(executed.Value().executionOrder.size() == 2U);
        CHECK(executed.Value().executionOrder[0].value == "step.compile");
        CHECK(executed.Value().executionOrder[1].value == "step.package");
        REQUIRE(executed.Value().outputs.size() == 2U);
        CHECK(executed.Value().outputs[0].id.value == "artifact.compiled");
        CHECK(executed.Value().outputs[1].id.value == "artifact.package");
        const PipelineArtifact *packaged = FindOutput(executed.Value(), "artifact.package");
        REQUIRE(packaged != nullptr);
        CHECK(packaged->bytes == Bytes({9U}));
    }

    TEST_CASE("Pipeline orders independent ready steps by phase then identity", "[unit][extensions][pipeline-step][headless]") {
        PipelineStepRegistry registry;
        std::vector<std::string> observed;
        const auto makeStep = [&observed](std::string id, std::string output) {
            return std::make_shared<TestStep>([&observed, id = std::move(id), output = std::move(output)](const PipelineStepContext &,
                                                                                                          PipelineOutputSink &sink,
                                                                                                          const CancellationToken &) {
                observed.push_back(id);
                const auto bytes = Bytes({1U});
                return sink.Write({output}, bytes);
            });
        };
        auto package = registry.Register(Descriptor("step.z-package", {}, {{"artifact.z"}}, {}, PipelinePhase::Package),
                                         makeStep("package", "artifact.z"));
        auto buildB = registry.Register(Descriptor("step.b-build", {}, {{"artifact.b"}}), makeStep("b", "artifact.b"));
        auto buildA = registry.Register(Descriptor("step.a-build", {}, {{"artifact.a"}}), makeStep("a", "artifact.a"));
        REQUIRE(package.HasValue());
        REQUIRE(buildB.HasValue());
        REQUIRE(buildA.HasValue());
        REQUIRE(registry.Execute({}, {}).HasValue());
        CHECK(observed == std::vector<std::string>{"a", "b", "package"});
    }

    TEST_CASE("Pipeline rejects dependency cycles missing steps and backward phases before callbacks",
              "[unit][extensions][pipeline-step][headless]") {
        const auto noOp = std::make_shared<TestStep>([](const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &) {
            return Result<void>::Success();
        });

        SECTION("cycle") {
            PipelineStepRegistry registry;
            auto a = registry.Register(Descriptor("step.a", {}, {}, {{"step.b"}}), noOp);
            auto b = registry.Register(Descriptor("step.b", {}, {}, {{"step.a"}}), noOp);
            REQUIRE(a.HasValue());
            REQUIRE(b.HasValue());
            RequireError(registry.Execute({}, {}), "pipeline_graph_cycle");
        }

        SECTION("missing dependency") {
            PipelineStepRegistry registry;
            auto registered = registry.Register(Descriptor("step.a", {}, {}, {{"step.missing"}}), noOp);
            REQUIRE(registered.HasValue());
            RequireError(registry.Execute({}, {}), "pipeline_graph_invalid");
        }

        SECTION("later phase dependency") {
            PipelineStepRegistry registry;
            auto package = registry.Register(Descriptor("step.package", {}, {}, {}, PipelinePhase::Package), noOp);
            auto build = registry.Register(Descriptor("step.build", {}, {}, {{"step.package"}}, PipelinePhase::Build), noOp);
            REQUIRE(package.HasValue());
            REQUIRE(build.HasValue());
            RequireError(registry.Execute({}, {}), "pipeline_graph_invalid");
        }
        CHECK(noOp->calls == 0U);
    }

    TEST_CASE("Pipeline discards staged output on provider failure and cancellation", "[unit][extensions][pipeline-step][headless]") {
        SECTION("typed failure preserves its cause") {
            PipelineStepRegistry registry;
            auto failing =
                std::make_shared<TestStep>([](const PipelineStepContext &, PipelineOutputSink &outputs, const CancellationToken &) {
                const auto staged = Bytes({1U, 2U, 3U});
                REQUIRE(outputs.Write({"artifact.partial"}, staged).HasValue());
                return Result<void>::Failure(MakeError(ExtensionErrors::InvocationFailed, "provider rejected input"));
            });
            auto registration = registry.Register(Descriptor("step.fails", {}, {{"artifact.partial"}}), failing);
            REQUIRE(registration.HasValue());
            const auto result = registry.Execute({}, {});
            RequireError(result, "pipeline_step_invocation_failed");
            REQUIRE(result.ErrorValue().cause.Get() != nullptr);
            CHECK(result.ErrorValue().cause.Get()->code.Value() == "invocation_failed");
        }

        SECTION("cancellation after staging publishes nothing") {
            PipelineStepRegistry registry;
            CancellationSource cancellation;
            auto cancelling = std::make_shared<TestStep>(
                [&cancellation](const PipelineStepContext &, PipelineOutputSink &outputs, const CancellationToken &) {
                const auto staged = Bytes({7U});
                REQUIRE(outputs.Write({"artifact.partial"}, staged).HasValue());
                cancellation.RequestCancellation();
                return Result<void>::Success();
            });
            auto never = std::make_shared<TestStep>([](const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &) {
                return Result<void>::Success();
            });
            auto first = registry.Register(Descriptor("step.a-cancels", {}, {{"artifact.partial"}}), cancelling);
            auto second = registry.Register(Descriptor("step.b-never", {}, {}, {{"step.a-cancels"}}), never);
            REQUIRE(first.HasValue());
            REQUIRE(second.HasValue());
            RequireError(registry.Execute({}, cancellation.Token()), "pipeline_run_cancelled");
            CHECK(never->calls == 0U);
        }
    }

    TEST_CASE("Pipeline output sink enforces the complete declared output contract", "[unit][extensions][pipeline-step][headless]") {
        const auto run = [](StepBody body, std::vector<PipelineArtifactId> outputs) {
            PipelineStepRegistry registry;
            auto step = std::make_shared<TestStep>(std::move(body));
            auto registration = registry.Register(Descriptor("step.output", {}, std::move(outputs)), step);
            REQUIRE(registration.HasValue());
            return registry.Execute({}, {});
        };

        RequireError(run(
                         [](const PipelineStepContext &, PipelineOutputSink &outputs, const CancellationToken &) {
            const auto bytes = Bytes({1U});
            return outputs.Write({"artifact.undeclared"}, bytes);
        }, {{"artifact.declared"}}),
                     "pipeline_step_invocation_failed");
        RequireError(run(
                         [](const PipelineStepContext &, PipelineOutputSink &outputs, const CancellationToken &) {
            const auto bytes = Bytes({1U});
            REQUIRE(outputs.Write({"artifact.output"}, bytes).HasValue());
            return outputs.Write({"artifact.output"}, bytes);
        }, {{"artifact.output"}}),
                     "pipeline_step_invocation_failed");
        RequireError(run(
                         [](const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &) {
            return Result<void>::Success();
        }, {{"artifact.missing"}}),
                     "pipeline_step_invocation_failed");
    }

    TEST_CASE("Pipeline validates descriptors inputs conflicts capacity and shutdown", "[unit][extensions][pipeline-step][headless]") {
        PipelineStepRegistry registry;
        const auto noOp = std::make_shared<TestStep>([](const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &) {
            return Result<void>::Success();
        });
        auto malformed = Descriptor("Invalid Step", {}, {});
        RequireError(registry.Register(std::move(malformed), noOp), "pipeline_step_registry_invalid");
        auto zeroGeneration = Descriptor("step.zero", {}, {}, {}, PipelinePhase::Build, 0U);
        RequireError(registry.Register(std::move(zeroGeneration), noOp), "pipeline_step_registry_invalid");
        auto overlapping = Descriptor("step.overlap", {{"artifact.same"}}, {{"artifact.same"}});
        RequireError(registry.Register(std::move(overlapping), noOp), "pipeline_step_registry_invalid");
        RequireError(registry.Register(Descriptor("step.null", {}, {}), {}), "pipeline_step_registry_invalid");

        auto first = registry.Register(Descriptor("step.first", {}, {{"artifact.output"}}), noOp);
        REQUIRE(first.HasValue());
        RequireError(registry.Register(Descriptor("step.first", {}, {}), noOp), "pipeline_step_registry_duplicate");
        RequireError(registry.Register(Descriptor("step.second", {}, {{"artifact.output"}}), noOp), "pipeline_step_registry_duplicate");

        const auto initialBytes = Bytes({1U});
        const std::array duplicateInitial{PipelineArtifactView{"artifact.same", initialBytes},
                                          PipelineArtifactView{"artifact.same", initialBytes}};
        RequireError(registry.Execute(duplicateInitial, {}), "pipeline_step_registry_invalid");
        const std::array conflictingInitial{PipelineArtifactView{"artifact.output", initialBytes}};
        RequireError(registry.Execute(conflictingInitial, {}), "pipeline_graph_invalid");
        const std::array missingInput{PipelineArtifactView{"artifact.input", initialBytes}};
        auto requiring = registry.Register(Descriptor("step.requires", {{"artifact.missing"}}, {}), noOp);
        REQUIRE(requiring.HasValue());
        RequireError(registry.Execute(missingInput, {}), "pipeline_graph_invalid");

        registry.BeginShutdown();
        CHECK(registry.IsShutdown());
        CHECK_FALSE(first.Value().IsRegistered());
        RequireError(registry.Register(Descriptor("step.after-shutdown", {}, {}), noOp), "pipeline_step_registry_shutdown");
        RequireError(registry.Execute({}, {}), "pipeline_step_registry_shutdown");
    }

    TEST_CASE("Pipeline registry enforces its hard publication bound", "[unit][extensions][pipeline-step][headless]") {
        PipelineStepRegistry registry;
        const auto noOp = std::make_shared<TestStep>([](const PipelineStepContext &, PipelineOutputSink &, const CancellationToken &) {
            return Result<void>::Success();
        });
        std::vector<PipelineStepRegistration> registrations;
        registrations.reserve(PipelineStepRegistry::MaximumProviders);
        for (std::size_t index = 0; index < PipelineStepRegistry::MaximumProviders; ++index) {
            auto registered = registry.Register(Descriptor("step.capacity-" + std::to_string(index), {}, {}), noOp);
            REQUIRE(registered.HasValue());
            registrations.push_back(std::move(registered).Value());
        }
        RequireError(registry.Register(Descriptor("step.overflow", {}, {}), noOp), "pipeline_step_registry_capacity_exceeded");
    }
}  // namespace Horo::Extensions::Tests
