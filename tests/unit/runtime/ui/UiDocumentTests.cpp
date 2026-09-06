#include "Horo/Runtime/Ui/UiDocument.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace Horo::Runtime::Ui {
    namespace {
        template <typename Id> Id IdWith(std::uint8_t marker) {
            SerializedUiId bytes{};
            bytes.back() = marker;
            auto result = Id::Create(bytes);
            REQUIRE(result.HasValue());
            return result.Value();
        }

        UiDocumentRevision Revision(std::uint64_t value = 1) {
            return UiDocumentRevision::Create(value).Value();
        }

        Assets::AssetId Asset(std::uint8_t marker) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = marker;
            return Assets::AssetId::FromBytes(bytes);
        }

        Assets::AssetTypeId Type(std::string_view value) {
            return Assets::AssetTypeId::Parse(value).Value();
        }

        UiCanvasDescriptor Canvas(std::uint8_t marker) {
            return {IdWith<UiCanvasId>(marker), IdWith<UiElementId>(static_cast<std::uint8_t>(marker + 64))};
        }

        UiDocument Document(const std::uint64_t revision = 1) {
            UiDocumentBuilder builder{IdWith<UiDocumentId>(1), Revision(revision)};
            REQUIRE(builder.AddCanvas(Canvas(2)).HasValue());
            REQUIRE(builder.RequireAsset({Asset(5), Type("core.texture"), false}).HasValue());
            REQUIRE(builder.RequireAsset({Asset(3), Type("core.font"), true}).HasValue());
            REQUIRE(builder.RequireAsset({Asset(5), Type("core.texture"), true}).HasValue());
            return std::move(builder).Build().Value();
        }

        TEST_CASE("UI document preserves canvas order and canonicalizes dependencies", "[runtime_ui][document]") {
            const UiDocument document = Document();
            REQUIRE(document.Canvases().size() == 1);
            REQUIRE(document.Dependencies().size() == 2);
            REQUIRE(document.Dependencies()[0].asset == Asset(3));
            REQUIRE(document.Dependencies()[1].asset == Asset(5));
            REQUIRE(document.Dependencies()[1].required);
        }

        TEST_CASE("UI document rejects malformed duplicate and conflicting input", "[runtime_ui][document]") {
            REQUIRE(UiDocumentBuilder{{}, Revision()}.Build().HasError());
            UiDocumentBuilder duplicate{IdWith<UiDocumentId>(1), Revision()};
            REQUIRE(duplicate.AddCanvas(Canvas(2)).HasValue());
            REQUIRE(duplicate.AddCanvas(Canvas(2)).HasValue());
            REQUIRE(std::move(duplicate).Build().HasError());
            UiDocumentBuilder conflict{IdWith<UiDocumentId>(1), Revision()};
            REQUIRE(conflict.RequireAsset({Asset(4), Type("core.font"), true}).HasValue());
            REQUIRE(conflict.RequireAsset({Asset(4), Type("core.texture"), true}).HasError());

            UiDocumentBuilder bounded{IdWith<UiDocumentId>(1), Revision()};
            for (std::uint8_t marker = 1; marker <= MaximumUiDocumentCanvases; ++marker)
                REQUIRE(bounded.AddCanvas(Canvas(marker)).HasValue());
            REQUIRE(bounded.AddCanvas(Canvas(65)).HasError());
        }

        TEST_CASE("Cooked UI document owns bytes separately from authoring", "[runtime_ui][document]") {
            const UiDocument document = Document();
            std::vector<std::uint8_t> bytes{1, 2, 3};
            auto cooked = CookedUiDocument::Create(document, bytes);
            REQUIRE(cooked.HasValue());
            bytes[0] = 9;
            REQUIRE(cooked.Value().Payload()[0] == 1);
            REQUIRE(cooked.Value().Dependencies().size() == document.Dependencies().size());
            REQUIRE(CookedUiDocument::Create(document, {}).HasError());
        }

        TEST_CASE("UI canvas asset references require complete stable evidence", "[runtime_ui][document]") {
            UiCanvasAssetReference reference{Asset(7), IdWith<UiDocumentId>(1), IdWith<UiCanvasId>(2), Revision()};
            REQUIRE(ValidateUiCanvasAssetReference(reference).HasValue());
            reference.asset = {};
            const auto invalid = ValidateUiCanvasAssetReference(reference);
            REQUIRE(invalid.HasError());
            REQUIRE(invalid.ErrorValue().code.Value() == UiErrors::CanvasReferenceInvalid.code.Value());
        }

        TEST_CASE("Runtime UI instance has explicit idempotent shutdown lifecycle", "[runtime_ui][instance]") {
            auto cooked = CookedUiDocument::Create(Document(), {1, 2});
            REQUIRE(cooked.HasValue());
            auto invalidCooked = CookedUiDocument::Create(Document(), {1});
            REQUIRE(invalidCooked.HasValue());
            const auto invalidInstance = UiRuntimeInstance::Create(std::move(invalidCooked).Value(), {});
            REQUIRE(invalidInstance.HasError());
            REQUIRE(invalidInstance.ErrorValue().code.Value() == UiErrors::HandleMalformed.code.Value());
            const auto owner = UiOwnershipGeneration::Create(8).Value();
            auto created = UiRuntimeInstance::Create(std::move(cooked).Value(), {owner, 1, 1});
            REQUIRE(created.HasValue());
            auto instance = std::move(created).Value();
            REQUIRE(instance.State() == UiRuntimeInstanceState::Prepared);
            REQUIRE(instance.Payload().size() == 2);
            REQUIRE(instance.Payload()[0] == 1);
            REQUIRE(instance.Payload()[1] == 2);
            REQUIRE(instance.Activate().HasValue());
            REQUIRE(instance.Activate().HasError());
            REQUIRE(instance.BeginRetirement().HasValue());
            instance.Shutdown();
            instance.Shutdown();
            REQUIRE(instance.State() == UiRuntimeInstanceState::Stopped);
            REQUIRE(instance.Dependencies().empty());
            REQUIRE(instance.Payload().empty());
            REQUIRE(instance.BeginRetirement().HasError());
        }

        TEST_CASE("Reloaded UI documents preserve isolated runtime snapshots", "[runtime_ui][instance][reload]") {
            auto firstCooked = CookedUiDocument::Create(Document(4), {4});
            auto reloadedCooked = CookedUiDocument::Create(Document(5), {5});
            REQUIRE(firstCooked.HasValue());
            REQUIRE(reloadedCooked.HasValue());
            const auto owner = UiOwnershipGeneration::Create(9).Value();
            auto firstResult = UiRuntimeInstance::Create(std::move(firstCooked).Value(), {owner, 1, 1});
            auto reloadedResult = UiRuntimeInstance::Create(std::move(reloadedCooked).Value(), {owner, 2, 1});
            REQUIRE(firstResult.HasValue());
            REQUIRE(reloadedResult.HasValue());
            auto first = std::move(firstResult).Value();
            auto reloaded = std::move(reloadedResult).Value();
            REQUIRE(first.DocumentId() == reloaded.DocumentId());
            REQUIRE(first.DocumentRevision() == Revision(4));
            REQUIRE(reloaded.DocumentRevision() == Revision(5));
            REQUIRE(first.Payload().front() == 4);
            REQUIRE(reloaded.Payload().front() == 5);
            REQUIRE(first.InstanceId() != reloaded.InstanceId());
        }

        TEST_CASE("UI document errors expose stable unique descriptors", "[runtime_ui][document][errors]") {
            const std::array descriptors{&UiErrors::DocumentInvalid,     &UiErrors::DocumentDuplicateIdentity,
                                         &UiErrors::DependencyInvalid,   &UiErrors::CapacityExceeded,
                                         &UiErrors::PayloadInvalid,      &UiErrors::CanvasReferenceInvalid,
                                         &UiErrors::InstanceStateInvalid};
            std::set<std::string_view> codes;
            for (const ErrorCodeDescriptor *descriptor : descriptors) {
                REQUIRE(descriptor->domain.Value() == "horo.runtime_ui");
                REQUIRE(codes.insert(descriptor->code.Value()).second);
                REQUIRE_FALSE(descriptor->summary.empty());
                REQUIRE_FALSE(descriptor->remediationHint.empty());
            }
        }
    }  // namespace
}  // namespace Horo::Runtime::Ui
