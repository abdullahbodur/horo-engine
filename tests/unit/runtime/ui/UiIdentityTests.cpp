#include "Horo/Runtime/Ui/UiIdentity.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

namespace Horo::Runtime::Ui {
    namespace {
        template <typename Value> Value RequireValue(Result<Value> result) {
            REQUIRE(result.HasValue());
            return result.Value();
        }

        template <typename Id> Id StableId(const std::uint8_t marker) {
            SerializedUiId bytes{};
            bytes.back() = marker;
            return RequireValue(Id::Create(bytes));
        }

        UiOwnershipGeneration Ownership(const std::uint64_t value) {
            return RequireValue(UiOwnershipGeneration::Create(value));
        }

        template <typename Revision> Revision MakeRevision(const std::uint64_t value) {
            return RequireValue(Revision::Create(value));
        }

        void ExpectError(const Result<void> &result, const ErrorCodeDescriptor &expected) {
            REQUIRE(result.HasError());
            REQUIRE(result.ErrorValue().domain.Value() == expected.domain.Value());
            REQUIRE(result.ErrorValue().code.Value() == expected.code.Value());
        }

        TEST_CASE("Runtime UI stable identities are typed persistent 128-bit values", "[runtime_ui][identity]") {
            REQUIRE(UiDocumentId::Create({}).HasError());
            REQUIRE(UiElementId::Create({}).HasError());
            REQUIRE(UiCanvasId::Create({}).HasError());

            const UiDocumentId document = StableId<UiDocumentId>(1);
            const UiElementId element = StableId<UiElementId>(1);
            const UiCanvasId canvas = StableId<UiCanvasId>(1);
            REQUIRE(document.IsValid());
            REQUIRE(element.IsValid());
            REQUIRE(canvas.IsValid());
            REQUIRE(document.Bytes().back() == 1);
            constexpr UiDocumentId invalid;
            static_assert(!invalid.IsValid());
            static_assert(invalid.Bytes() == SerializedUiId{});
            static_assert(!std::is_same_v<UiDocumentId, UiElementId>);
            static_assert(!std::is_same_v<UiElementId, UiCanvasId>);
            static_assert(std::is_trivially_copyable_v<UiDocumentId>);
        }

        TEST_CASE("Runtime UI handles have exact 128-bit owner and slot identity", "[runtime_ui][handle]") {
            const UiOwnershipGeneration owner = Ownership(17);
            REQUIRE(UiOwnershipGeneration::Create(0).HasError());
            REQUIRE_FALSE(UiElementHandle{}.IsValid());
            REQUIRE_FALSE(UiElementHandle{owner, 0, 1}.IsValid());
            REQUIRE_FALSE(UiElementHandle{owner, 1, 0}.IsValid());
            REQUIRE(UiElementHandle{owner, 1, 1}.IsValid());
            static_assert(sizeof(UiElementHandle) == 16);
            static_assert(std::is_trivially_copyable_v<UiElementHandle>);
            static_assert(!std::is_same_v<RuntimeUiInstanceId, UiCanvasInstanceId>);
            static_assert(!std::is_same_v<UiCanvasInstanceId, UiElementHandle>);
        }

        TEST_CASE("Runtime UI rejects malformed foreign and stale handles", "[runtime_ui][handle]") {
            const UiOwnershipGeneration owner = Ownership(23);
            const UiElementHandle handle{owner, 4, 7};
            REQUIRE(ValidateUiHandleOwner(handle, owner).HasValue());
            REQUIRE(ValidateUiHandleResidency(handle, owner, 4, 7, true).HasValue());

            ExpectError(ValidateUiHandleOwner(UiElementHandle{}, owner), UiErrors::HandleMalformed);
            ExpectError(ValidateUiHandleOwner(handle, Ownership(24)), UiErrors::HandleOwnerMismatch);
            ExpectError(ValidateUiHandleResidency(handle, owner, 4, 7, false), UiErrors::HandleStale);
            ExpectError(ValidateUiHandleResidency(handle, owner, 5, 7, true), UiErrors::HandleStale);
            ExpectError(ValidateUiHandleResidency(handle, owner, 4, 8, true), UiErrors::HandleStale);
            ExpectError(ValidateUiHandleResidency(handle, owner, 4, 0, true), UiErrors::HandleStale);
        }

        TEST_CASE("Runtime UI owner replacement invalidates otherwise identical handles", "[runtime_ui][handle]") {
            const UiElementHandle retired{Ownership(31), 9, 2};
            const UiElementHandle replacement{Ownership(32), retired.slot, retired.generation};
            REQUIRE(retired != replacement);
            ExpectError(ValidateUiHandleOwner(retired, replacement.ownership), UiErrors::HandleOwnerMismatch);
            REQUIRE(ValidateUiHandleResidency(replacement, replacement.ownership, 9, 2, true).HasValue());
        }

        TEST_CASE("Runtime UI revisions compare exactly and reject stale commands", "[runtime_ui][revision]") {
            REQUIRE(UiDocumentRevision::Create(0).HasError());
            const UiDocumentRevision first = MakeRevision<UiDocumentRevision>(1);
            const UiDocumentRevision second = MakeRevision<UiDocumentRevision>(2);
            REQUIRE(first.Compare(first) == UiRevisionRelation::Equal);
            REQUIRE(first.Compare(second) == UiRevisionRelation::Older);
            REQUIRE(second.Compare(first) == UiRevisionRelation::Newer);
            REQUIRE(ValidateExpectedUiRevision(second, second).HasValue());
            ExpectError(ValidateExpectedUiRevision(first, second), UiErrors::RevisionStale);
            ExpectError(ValidateExpectedUiRevision(UiDocumentRevision{}, second), UiErrors::RevisionInvalid);
            static_assert(!std::is_same_v<UiDocumentRevision, UiRuntimeTreeRevision>);
            static_assert(!std::is_same_v<UiRuntimeTreeRevision, UiInteractionRevision>);
        }

        TEST_CASE("Runtime UI revisions advance without wrapping", "[runtime_ui][revision]") {
            const UiRuntimeTreeRevision current = MakeRevision<UiRuntimeTreeRevision>(41);
            const auto next = current.Next();
            REQUIRE(next.HasValue());
            REQUIRE(next.Value().Value() == 42);
            REQUIRE(UiRuntimeTreeRevision{}.Next().HasError());
            const auto last = MakeRevision<UiRuntimeTreeRevision>(std::numeric_limits<std::uint64_t>::max());
            const auto exhausted = last.Next();
            REQUIRE(exhausted.HasError());
            REQUIRE(exhausted.ErrorValue().code.Value() == UiErrors::GenerationExhausted.code.Value());
        }

        TEST_CASE("Runtime UI identity errors expose stable unique descriptors", "[runtime_ui][errors]") {
            const std::array<std::pair<const ErrorCodeDescriptor *, std::string_view>, 8> cases{{
                {&UiErrors::IdentityInvalid, "runtime_ui.identity.invalid"},
                {&UiErrors::OwnershipGenerationInvalid, "runtime_ui.ownership_generation.invalid"},
                {&UiErrors::HandleMalformed, "runtime_ui.handle.malformed"},
                {&UiErrors::HandleOwnerMismatch, "runtime_ui.handle.owner_mismatch"},
                {&UiErrors::HandleStale, "runtime_ui.handle.stale"},
                {&UiErrors::RevisionInvalid, "runtime_ui.revision.invalid"},
                {&UiErrors::RevisionStale, "runtime_ui.revision.stale"},
                {&UiErrors::GenerationExhausted, "runtime_ui.generation.exhausted"},
            }};
            std::set<std::string_view> unique;
            for (const auto &[descriptor, code] : cases) {
                REQUIRE(descriptor->domain.Value() == "horo.runtime_ui");
                REQUIRE(descriptor->code.Value() == code);
                REQUIRE(unique.insert(descriptor->code.Value()).second);
                REQUIRE_FALSE(descriptor->summary.empty());
                REQUIRE_FALSE(descriptor->remediationHint.empty());
            }
            REQUIRE(UiErrors::RevisionStale.retryable);
            REQUIRE(UiErrors::GenerationExhausted.defaultSeverity == ErrorSeverity::Critical);
        }
    }  // namespace
}  // namespace Horo::Runtime::Ui
