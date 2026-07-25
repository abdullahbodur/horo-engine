#include <catch2/catch_test_macros.hpp>

#include "Horo/Editor/AssetImportModal.h"
#include "Horo/Assets/AssetImporter.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorModalHost.h"
#include "Horo/Foundation/JobSystem.h"
#include "Horo/Runtime/Input.h"

#include <functional>
#include <memory>
#include <vector>

namespace
{
using namespace Horo;
using namespace Horo::Editor;
using namespace Horo::Assets;

class TestImporter final : public IAssetImporter
{
  public:
    [[nodiscard]] Result<PreparedAssetImport> Import(
        const AssetImportInput &input,
        const CancellationToken &cancellation) const override
    {
        PreparedAssetImport result;
        result.type = AssetTypeId::Parse("core.mesh").Value();
        result.editorPayload.assign(input.sourceBytes.begin(), input.sourceBytes.end());
        return Result<PreparedAssetImport>::Success(std::move(result));
    }
};

/** @brief Test double that overrides Draw for headless testing. */
class TestAssetImportModal : public AssetImportModal
{
  public:
    using AssetImportModal::AssetImportModal;

    ModalFrameResult Draw() override
    {
        if (!m_preparedCalled && m_prepareFn)
        {
            m_prepareFn();
            m_preparedCalled = true;
        }
        return ModalFrameResult::None();
    }

    std::function<void()> m_prepareFn;
    bool m_preparedCalled{false};
};

} // namespace

TEST_CASE("AssetImportModal lifecycle: open, begin, prepare, close", "[native]")
{
    EditorDataBus events;
    Input::InputRouter inputRouter;
    EditorModalHost modalHost{events, inputRouter};
    const auto &fonts = *reinterpret_cast<const Theme::Fonts *>(static_cast<std::uintptr_t>(1));
    JobSystem jobs;

    // Build importer catalog
    AssetImporterCatalog catalog;
    REQUIRE((catalog
                 .Register(AssetImporterContribution{
                     .contributionId = "test.obj",
                     .packageId = "test",
                     .moduleId = "test",
                     .version = "1.0",
                     .fileExtensions = {"obj"},
                     .assetTypes = {AssetTypeId::Parse("core.mesh").Value()},
                     .strategy = std::make_shared<const TestImporter>(),
                 })
                 .HasValue()));
    auto catSnapshot = catalog.Publish();
    REQUIRE((catSnapshot.HasValue()));

    auto modal = std::make_unique<TestAssetImportModal>(fonts, jobs, catSnapshot.Value());
    auto *modalPtr = modal.get();

    bool prepared = false;
    modalPtr->m_prepareFn = [&]() {
        CancellationToken cancellation;
        auto result = modalPtr->PrepareImport(cancellation);
        REQUIRE((result.HasValue()));
        prepared = true;
    };

    auto openResult = modalHost.OpenRoot(std::move(modal));
    REQUIRE((openResult.HasValue()));

    // Begin import
    CancellationToken cancellation;
    auto beginResult = modalPtr->BeginImport(
        {std::filesystem::path("/tmp/test/cube.obj")},
        std::filesystem::path("/tmp/test"),
        cancellation);
    REQUIRE((beginResult.HasValue()));

    auto &snap = modalPtr->Snapshot();
    REQUIRE((snap.items.size() == 1));

    // Draw triggers prepare
    modalHost.OnUpdate(0.016f);
    modalHost.Draw();

    REQUIRE((prepared));
    REQUIRE((modalPtr->Snapshot().phase == AssetImportPhase::ReadyToCommit));

    // Close
    auto closeResult = modalHost.RequestClose(modalPtr->Id(), ModalCloseReason::Completed);
    REQUIRE((closeResult.HasValue()));
    modalHost.OnUpdate(0.016f);
}
