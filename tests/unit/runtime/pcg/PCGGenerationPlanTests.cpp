#include "Horo/PCG/PCGErrors.h"
#include "Horo/PCG/PCGGenerationPlan.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace Horo::PCG {
    namespace {
        template <typename Identity> [[nodiscard]] Identity Id(const std::uint64_t value) {
            auto created = Identity::Create(value);
            REQUIRE_FALSE(created.HasError());
            return std::move(created).Value();
        }

        template <typename T> void RequireFailureCode(const Result<T> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE_FALSE(result.HasValue());
            const auto &failure = result.ErrorValue();
            INFO("expected " << expected.domain.Value() << ':' << expected.code.Value());
            CHECK(failure.domain.Value() == expected.domain.Value());
            CHECK(failure.code.Value() == expected.code.Value());
        }

        [[nodiscard]] PCGGenerationDigest Digest(const std::uint8_t value) {
            PCGGenerationDigest digest{};
            digest.bytes.fill(value);
            return digest;
        }

        [[nodiscard]] PCGCapabilitySet Capabilities(std::initializer_list<PCGCapability> values) {
            const std::vector<PCGCapability> storage(values);
            auto result = PCGCapabilitySet::Create(storage);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        [[nodiscard]] ExecutionId Execution(const std::uint64_t revision = 1, const std::uint64_t execution = 20) {
            return {{Id<GraphId>(10), Id<GraphRevision>(revision)}, Id<ExecutionValue>(execution)};
        }

        [[nodiscard]] PCGGenerationTargetReceipt Receipt(const std::uint64_t ownerGeneration = 5,
                                                         const std::uint64_t capabilityGeneration = 7) {
            return {Id<GenerationValidationReceiptId>(40), Id<GenerationTargetOwnerId>(41), ownerGeneration, capabilityGeneration,
                    Capabilities({PCGCapability::Validation, PCGCapability::SceneOutput})};
        }

        [[nodiscard]] PCGGeneratedOutputProvenance Provenance(const std::uint64_t logical = 100, const std::uint64_t setRevision = 1,
                                                              const ExecutionId execution = Execution()) {
            return {Id<GenerationLogicalOutputId>(logical),
                    {execution, Id<NodeId>(11), Id<PinId>(12), Id<SourceSampleId>(13), 0},
                    Id<GenerationLineageId>(30),
                    Id<GeneratedSetId>(31),
                    setRevision,
                    5,
                    Id<GenerationTargetOwnerId>(41),
                    Id<GenerationCellId>(50)};
        }

        [[nodiscard]] PCGOutputDelta CreateDelta(const std::uint64_t logical = 100) {
            return {PCGOutputDeltaKind::Create,       Provenance(logical), 0, {}, Digest(static_cast<std::uint8_t>(logical)),
                    {Id<GenerationDependencyId>(61)}, {5, 20, 30, 40}};
        }

        [[nodiscard]] PCGGenerationPlanCandidate Candidate() {
            return {CurrentPCGGenerationPlanVersion,
                    Id<GenerationPlanId>(21),
                    Execution(),
                    1234,
                    Id<GenerationLineageId>(30),
                    Id<GeneratedSetId>(31),
                    1,
                    Id<GenerationCellId>(50),
                    Id<GenerationTargetOwnerId>(41),
                    Capabilities({PCGCapability::SceneOutput}),
                    Receipt(),
                    {{Id<GenerationDependencyId>(61), 4, Digest(6)}},
                    {CreateDelta()}};
        }

        [[nodiscard]] PCGGenerationPlanContext Context(std::span<const PCGOwnedGeneratedOutput> owned = {}) {
            return {PCGGenerationPlanAdmission::Accepting, Receipt(), owned, {}};
        }

        [[nodiscard]] PCGOwnedGeneratedOutput Owned(const std::uint64_t logical = 100, const std::uint64_t setRevision = 1,
                                                    const ExecutionId execution = Execution()) {
            return {Provenance(logical, setRevision, execution), Digest(8)};
        }

        [[nodiscard]] PCGGenerationPlanCandidate ReplacementCandidate(const PCGOutputDeltaKind kind, const PCGOwnedGeneratedOutput &owned) {
            auto candidate = Candidate();
            candidate.plan = Id<GenerationPlanId>(22);
            candidate.execution = Execution(2, 21);
            candidate.setRevision = 2;
            candidate.outputs[0].kind = kind;
            candidate.outputs[0].provenance = Provenance(100, 2, candidate.execution);
            candidate.outputs[0].expectedSetRevision = owned.provenance.setRevision;
            candidate.outputs[0].priorContent = owned.content;
            candidate.outputs[0].desiredContent = kind == PCGOutputDeltaKind::Remove ? PCGGenerationDigest{} : Digest(9);
            return candidate;
        }
    }  // namespace

    TEST_CASE("PCG generation plan owns canonical immutable output and dependency data", "[unit][pcg][generation-plan]") {
        auto candidate = Candidate();
        candidate.dependencies.insert(candidate.dependencies.begin(), {Id<GenerationDependencyId>(60), 3, Digest(5)});
        candidate.outputs.insert(candidate.outputs.begin(), CreateDelta(101));
        candidate.outputs[0].dependencies = {Id<GenerationDependencyId>(61), Id<GenerationDependencyId>(60)};
        auto source = candidate;

        auto result = CreatePCGGenerationPlan(std::move(candidate), Context());
        REQUIRE(result.HasValue());
        const auto plan = result.Value();
        source.outputs[0].desiredContent.bytes.fill(99);
        source.dependencies.clear();

        CHECK(plan.Id() == Id<GenerationPlanId>(21));
        CHECK(plan.Version() == CurrentPCGGenerationPlanVersion);
        CHECK(plan.Execution() == Execution());
        CHECK(plan.Seed() == 1234);
        CHECK(plan.Lineage() == Id<GenerationLineageId>(30));
        CHECK(plan.Set() == Id<GeneratedSetId>(31));
        CHECK(plan.SetRevision() == 1);
        CHECK(plan.Cell() == Id<GenerationCellId>(50));
        CHECK(plan.Target() == Receipt());
        CHECK(plan.RequiredCapabilities().Contains(PCGCapability::SceneOutput));
        REQUIRE(plan.Dependencies().size() == 2);
        CHECK(plan.Dependencies()[0].id == Id<GenerationDependencyId>(60));
        REQUIRE(plan.Outputs().size() == 2);
        CHECK(plan.Outputs()[0].provenance.logicalOutput == Id<GenerationLogicalOutputId>(100));
        CHECK(plan.Outputs()[1].dependencies[0] == Id<GenerationDependencyId>(60));
        CHECK(plan.Resources() == PCGGenerationResourceEstimate{10, 40, 60, 80});
    }

    TEST_CASE("PCG generation plan rejects stale target and missing target capability", "[unit][pcg][generation-plan]") {
        auto staleOwner = Candidate();
        staleOwner.validation.ownerGeneration = 4;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(staleOwner), Context()), PCGErrors::GenerationPlanStale);

        auto staleCapabilities = Candidate();
        staleCapabilities.validation.capabilityGeneration = 6;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(staleCapabilities), Context()), PCGErrors::GenerationPlanStale);

        auto unsupported = Candidate();
        unsupported.requiredCapabilities = Capabilities({PCGCapability::TerrainOutput});
        RequireFailureCode(CreatePCGGenerationPlan(std::move(unsupported), Context()), PCGErrors::UnsupportedCapability);

        auto wrongOwner = Candidate();
        wrongOwner.targetOwner = Id<GenerationTargetOwnerId>(99);
        RequireFailureCode(CreatePCGGenerationPlan(std::move(wrongOwner), Context()), PCGErrors::GenerationPlanInvalid);

        auto versionSkew = Candidate();
        versionSkew.version = {2, 0};
        RequireFailureCode(CreatePCGGenerationPlan(std::move(versionSkew), Context()), PCGErrors::GenerationPlanInvalid);
    }

    TEST_CASE("PCG generation plan lifecycle gates allocate and publish nothing", "[unit][pcg][generation-plan]") {
        for (const auto admission : {PCGGenerationPlanAdmission::CancellationRequested, PCGGenerationPlanAdmission::ShuttingDown}) {
            auto context = Context();
            context.admission = admission;
            RequireFailureCode(CreatePCGGenerationPlan(Candidate(), context), PCGErrors::GenerationPlanLifecycleUnavailable);
        }
        auto invalid = Context();
        invalid.limits.maximumOutputs = 0;
        RequireFailureCode(CreatePCGGenerationPlan(Candidate(), invalid), PCGErrors::GenerationPlanInvalid);
    }

    TEST_CASE("PCG generation plan validates canonical dependency closure and delta shape", "[unit][pcg][generation-plan]") {
        auto duplicateDependency = Candidate();
        duplicateDependency.dependencies.push_back(duplicateDependency.dependencies.front());
        RequireFailureCode(CreatePCGGenerationPlan(std::move(duplicateDependency), Context()), PCGErrors::GenerationPlanInvalid);

        auto unknownDependency = Candidate();
        unknownDependency.outputs[0].dependencies[0] = Id<GenerationDependencyId>(99);
        RequireFailureCode(CreatePCGGenerationPlan(std::move(unknownDependency), Context()), PCGErrors::GenerationPlanInvalid);

        auto duplicateOutput = Candidate();
        duplicateOutput.outputs.push_back(duplicateOutput.outputs.front());
        RequireFailureCode(CreatePCGGenerationPlan(std::move(duplicateOutput), Context()), PCGErrors::GenerationPlanInvalid);

        auto malformedCreate = Candidate();
        malformedCreate.outputs[0].priorContent = Digest(3);
        RequireFailureCode(CreatePCGGenerationPlan(std::move(malformedCreate), Context()), PCGErrors::GenerationPlanInvalid);

        auto mismatchedExecution = Candidate();
        mismatchedExecution.outputs[0].provenance.sourceOutput.execution = Execution(2);
        RequireFailureCode(CreatePCGGenerationPlan(std::move(mismatchedExecution), Context()), PCGErrors::GenerationPlanInvalid);

        auto mismatchedOwnerGeneration = Candidate();
        mismatchedOwnerGeneration.outputs[0].provenance.ownershipGeneration = 4;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(mismatchedOwnerGeneration), Context()),
                           PCGErrors::GenerationOwnershipMismatch);
    }

    TEST_CASE("PCG generation plan enforces checked independent count and resource bounds", "[unit][pcg][generation-plan]") {
        auto outputBound = Context();
        outputBound.limits.maximumOutputs = 1;
        auto twoOutputs = Candidate();
        twoOutputs.outputs.push_back(CreateDelta(101));
        RequireFailureCode(CreatePCGGenerationPlan(std::move(twoOutputs), outputBound), PCGErrors::GenerationPlanCapacityExceeded);

        auto dependencyBound = Context();
        dependencyBound.limits.maximumDependencies = 1;
        auto twoDependencies = Candidate();
        twoDependencies.dependencies.push_back({Id<GenerationDependencyId>(62), 5, Digest(7)});
        RequireFailureCode(CreatePCGGenerationPlan(std::move(twoDependencies), dependencyBound), PCGErrors::GenerationPlanCapacityExceeded);

        auto workBound = Context();
        workBound.limits.maximumWorkUnits = 4;
        RequireFailureCode(CreatePCGGenerationPlan(Candidate(), workBound), PCGErrors::GenerationPlanCapacityExceeded);

        auto overflow = Candidate();
        overflow.outputs[0].resources.residentBytes = std::numeric_limits<std::uint64_t>::max();
        RequireFailureCode(CreatePCGGenerationPlan(std::move(overflow), Context()), PCGErrors::GenerationPlanCapacityExceeded);

        auto exact = Context();
        exact.limits.maximumWorkUnits = 5;
        exact.limits.maximumChargedBytes = 90;
        REQUIRE(CreatePCGGenerationPlan(Candidate(), exact).HasValue());
    }

    TEST_CASE("PCG generation updates and removals require exact target-owned provenance", "[unit][pcg][generation-plan]") {
        const auto owned = Owned();
        const std::vector ownership{owned};
        const auto context = Context(ownership);

        auto createCollision = Candidate();
        RequireFailureCode(CreatePCGGenerationPlan(std::move(createCollision), context), PCGErrors::GenerationOwnershipMismatch);

        auto update = ReplacementCandidate(PCGOutputDeltaKind::Update, owned);
        REQUIRE(CreatePCGGenerationPlan(std::move(update), context).HasValue());

        auto removal = ReplacementCandidate(PCGOutputDeltaKind::Remove, owned);
        REQUIRE(CreatePCGGenerationPlan(std::move(removal), context).HasValue());

        auto wrongContent = ReplacementCandidate(PCGOutputDeltaKind::Remove, owned);
        wrongContent.outputs[0].priorContent = Digest(4);
        RequireFailureCode(CreatePCGGenerationPlan(std::move(wrongContent), context), PCGErrors::GenerationOwnershipMismatch);

        auto wrongRevision = ReplacementCandidate(PCGOutputDeltaKind::Remove, owned);
        wrongRevision.outputs[0].expectedSetRevision = owned.provenance.setRevision + 1;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(wrongRevision), context), PCGErrors::GenerationOwnershipMismatch);

        auto wrongScope = ReplacementCandidate(PCGOutputDeltaKind::Remove, owned);
        wrongScope.cell = Id<GenerationCellId>(51);
        wrongScope.outputs[0].provenance.cell = wrongScope.cell;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(wrongScope), context), PCGErrors::GenerationOwnershipMismatch);

        auto wrongOwnership = ReplacementCandidate(PCGOutputDeltaKind::Remove, owned);
        wrongOwnership.outputs[0].provenance.ownershipGeneration = 6;
        RequireFailureCode(CreatePCGGenerationPlan(std::move(wrongOwnership), context), PCGErrors::GenerationOwnershipMismatch);
    }

    TEST_CASE("PCG generation plan replacement preserves old roots and advances exact lineage", "[unit][pcg][generation-plan]") {
        auto original = CreatePCGGenerationPlan(Candidate(), Context());
        REQUIRE(original.HasValue());
        const auto retained = original.Value();
        const auto owned = Owned();
        const std::vector ownership{owned};

        auto replacementCandidate = ReplacementCandidate(PCGOutputDeltaKind::Update, owned);
        auto replacement = ReplacePCGGenerationPlan(retained, std::move(replacementCandidate), Context(ownership));
        REQUIRE(replacement.HasValue());
        CHECK(retained.SetRevision() == 1);
        CHECK(retained.Outputs()[0].desiredContent == Digest(100));
        CHECK(replacement.Value().SetRevision() == 2);
        CHECK(replacement.Value().Outputs()[0].desiredContent == Digest(9));

        auto staleRevision = ReplacementCandidate(PCGOutputDeltaKind::Update, owned);
        staleRevision.setRevision = 1;
        staleRevision.outputs[0].provenance.setRevision = 1;
        RequireFailureCode(ReplacePCGGenerationPlan(retained, std::move(staleRevision), Context(ownership)),
                           PCGErrors::GenerationPlanStale);

        auto wrongLineage = ReplacementCandidate(PCGOutputDeltaKind::Update, owned);
        wrongLineage.lineage = Id<GenerationLineageId>(88);
        wrongLineage.outputs[0].provenance.lineage = wrongLineage.lineage;
        RequireFailureCode(ReplacePCGGenerationPlan(retained, std::move(wrongLineage), Context(ownership)), PCGErrors::GenerationPlanStale);

        auto olderGraph = ReplacementCandidate(PCGOutputDeltaKind::Update, owned);
        olderGraph.execution = Execution(1, 21);
        olderGraph.outputs[0].provenance.sourceOutput.execution = olderGraph.execution;
        REQUIRE(ReplacePCGGenerationPlan(retained, std::move(olderGraph), Context(ownership)).HasValue());
    }

    TEST_CASE("PCG generation plan rejects malformed target-owned snapshots", "[unit][pcg][generation-plan]") {
        auto invalid = Owned();
        invalid.provenance.targetOwner = {};
        const std::vector invalidOwnership{invalid};
        RequireFailureCode(CreatePCGGenerationPlan(Candidate(), Context(invalidOwnership)), PCGErrors::GenerationPlanInvalid);

        const auto owned = Owned();
        const std::vector duplicateOwnership{owned, owned};
        RequireFailureCode(CreatePCGGenerationPlan(Candidate(), Context(duplicateOwnership)), PCGErrors::GenerationPlanInvalid);
    }

    TEST_CASE("PCG generation plan errors are stable unique public descriptors", "[unit][pcg][generation-plan][errors]") {
        const std::array errors{&PCGErrors::GenerationPlanInvalid, &PCGErrors::GenerationPlanCapacityExceeded,
                                &PCGErrors::GenerationPlanStale, &PCGErrors::GenerationOwnershipMismatch,
                                &PCGErrors::GenerationPlanLifecycleUnavailable};
        std::set<std::string> codes;
        for (const auto *error : errors) {
            CHECK(error->domain.Value() == "horo.pcg");
            CHECK(error->code.Value().starts_with("pcg.generation_plan."));
            CHECK(codes.insert(std::string(error->code.Value())).second);
        }
    }
}  // namespace Horo::PCG
