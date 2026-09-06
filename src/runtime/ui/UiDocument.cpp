#include "Horo/Runtime/Ui/UiDocument.h"

#include <algorithm>
#include <utility>

namespace Horo::Runtime::Ui {
    namespace {
        template <typename T = void> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }
    }  // namespace

    /** @copydoc UiDocument::UiDocument */
    UiDocument::UiDocument(UiDocumentId id, UiDocumentRevision revision, std::vector<UiCanvasDescriptor> canvases,
                           std::vector<UiAssetDependency> dependencies) noexcept
        : id_(id), revision_(revision), canvases_(std::move(canvases)), dependencies_(std::move(dependencies)) {}

    /** @copydoc UiDocument::Id */
    UiDocumentId UiDocument::Id() const noexcept {
        return id_;
    }

    /** @copydoc UiDocument::Revision */
    UiDocumentRevision UiDocument::Revision() const noexcept {
        return revision_;
    }

    /** @copydoc UiDocument::Canvases */
    std::span<const UiCanvasDescriptor> UiDocument::Canvases() const noexcept {
        return canvases_;
    }

    /** @copydoc UiDocument::Dependencies */
    std::span<const UiAssetDependency> UiDocument::Dependencies() const noexcept {
        return dependencies_;
    }

    /** @copydoc UiDocumentBuilder::UiDocumentBuilder */
    UiDocumentBuilder::UiDocumentBuilder(UiDocumentId id, UiDocumentRevision revision) noexcept : id_(id), revision_(revision) {}

    /** @copydoc UiDocumentBuilder::AddCanvas */
    Result<void> UiDocumentBuilder::AddCanvas(UiCanvasDescriptor canvas) {
        if (canvases_.size() == MaximumUiDocumentCanvases)
            return Failure(UiErrors::CapacityExceeded);
        canvases_.push_back(std::move(canvas));
        return Result<void>::Success();
    }

    /** @copydoc UiDocumentBuilder::RequireAsset */
    Result<void> UiDocumentBuilder::RequireAsset(UiAssetDependency dependency) {
        if (!dependency.asset.IsValid() || dependency.expectedType.Value().empty())
            return Failure(UiErrors::DependencyInvalid);
        const auto found = std::ranges::find(dependencies_, dependency.asset, &UiAssetDependency::asset);
        if (found == dependencies_.end()) {
            if (dependencies_.size() == MaximumUiDocumentDependencies)
                return Failure(UiErrors::CapacityExceeded);
            dependencies_.push_back(std::move(dependency));
            return Result<void>::Success();
        }
        if (found->expectedType != dependency.expectedType)
            return Failure(UiErrors::DependencyInvalid);
        found->required = found->required || dependency.required;
        return Result<void>::Success();
    }

    /** @copydoc UiDocumentBuilder::Build */
    Result<UiDocument> UiDocumentBuilder::Build() && {
        if (!id_.IsValid() || !revision_.IsValid() || canvases_.empty())
            return Failure<UiDocument>(UiErrors::DocumentInvalid);
        for (std::size_t index = 0; index < canvases_.size(); ++index) {
            const auto &canvas = canvases_[index];
            if (!canvas.id.IsValid() || !canvas.rootElement.IsValid())
                return Failure<UiDocument>(UiErrors::DocumentInvalid);
            for (std::size_t previous = 0; previous < index; ++previous)
                if (canvases_[previous].id == canvas.id || canvases_[previous].rootElement == canvas.rootElement)
                    return Failure<UiDocument>(UiErrors::DocumentDuplicateIdentity);
        }
        std::ranges::sort(dependencies_, {}, &UiAssetDependency::asset);
        return Result<UiDocument>::Success(UiDocument{id_, revision_, std::move(canvases_), std::move(dependencies_)});
    }

    /** @copydoc CookedUiDocument::CookedUiDocument */
    CookedUiDocument::CookedUiDocument(UiDocumentId id, UiDocumentRevision revision, std::vector<UiAssetDependency> dependencies,
                                       std::vector<std::uint8_t> payload) noexcept
        : id_(id), revision_(revision), dependencies_(std::move(dependencies)), payload_(std::move(payload)) {}

    /** @copydoc CookedUiDocument::Create */
    Result<CookedUiDocument> CookedUiDocument::Create(const UiDocument &document, std::vector<std::uint8_t> payload) {
        if (payload.empty())
            return Failure<CookedUiDocument>(UiErrors::PayloadInvalid);
        if (payload.size() > MaximumCookedUiDocumentBytes)
            return Failure<CookedUiDocument>(UiErrors::CapacityExceeded);
        return Result<CookedUiDocument>::Success(CookedUiDocument{document.Id(),
                                                                  document.Revision(),
                                                                  {document.Dependencies().begin(), document.Dependencies().end()},
                                                                  std::move(payload)});
    }

    /** @copydoc CookedUiDocument::Id */
    UiDocumentId CookedUiDocument::Id() const noexcept {
        return id_;
    }

    /** @copydoc CookedUiDocument::SourceRevision */
    UiDocumentRevision CookedUiDocument::SourceRevision() const noexcept {
        return revision_;
    }

    /** @copydoc CookedUiDocument::Dependencies */
    std::span<const UiAssetDependency> CookedUiDocument::Dependencies() const noexcept {
        return dependencies_;
    }

    /** @copydoc CookedUiDocument::Payload */
    std::span<const std::uint8_t> CookedUiDocument::Payload() const noexcept {
        return payload_;
    }

    /** @copydoc ValidateUiCanvasAssetReference */
    Result<void> ValidateUiCanvasAssetReference(const UiCanvasAssetReference &reference) {
        if (!reference.asset.IsValid() || !reference.document.IsValid() || !reference.canvas.IsValid() ||
            !reference.minimumRevision.IsValid())
            return Failure(UiErrors::CanvasReferenceInvalid);
        return Result<void>::Success();
    }

    /** @copydoc UiRuntimeInstance::UiRuntimeInstance */
    UiRuntimeInstance::UiRuntimeInstance(UiDocumentId document, UiDocumentRevision revision, std::vector<UiAssetDependency> dependencies,
                                         std::vector<std::uint8_t> payload, RuntimeUiInstanceId instance) noexcept
        : document_(document), revision_(revision), dependencies_(std::move(dependencies)), payload_(std::move(payload)),
          instance_(instance) {}

    /** @copydoc UiRuntimeInstance::Create */
    Result<UiRuntimeInstance> UiRuntimeInstance::Create(CookedUiDocument document, RuntimeUiInstanceId instance) {
        if (!instance.IsValid())
            return Failure<UiRuntimeInstance>(UiErrors::HandleMalformed);
        if (!document.Id().IsValid() || !document.SourceRevision().IsValid() || document.Payload().empty())
            return Failure<UiRuntimeInstance>(UiErrors::PayloadInvalid);
        return Result<UiRuntimeInstance>::Success(UiRuntimeInstance{document.Id(), document.SourceRevision(),
                                                                    std::move(document.dependencies_), std::move(document.payload_),
                                                                    instance});
    }

    /** @copydoc UiRuntimeInstance::InstanceId */
    RuntimeUiInstanceId UiRuntimeInstance::InstanceId() const noexcept {
        return instance_;
    }

    /** @copydoc UiRuntimeInstance::DocumentId */
    UiDocumentId UiRuntimeInstance::DocumentId() const noexcept {
        return document_;
    }

    /** @copydoc UiRuntimeInstance::DocumentRevision */
    UiDocumentRevision UiRuntimeInstance::DocumentRevision() const noexcept {
        return revision_;
    }

    /** @copydoc UiRuntimeInstance::State */
    UiRuntimeInstanceState UiRuntimeInstance::State() const noexcept {
        return state_;
    }

    /** @copydoc UiRuntimeInstance::Dependencies */
    std::span<const UiAssetDependency> UiRuntimeInstance::Dependencies() const noexcept {
        return dependencies_;
    }

    /** @copydoc UiRuntimeInstance::Payload */
    std::span<const std::uint8_t> UiRuntimeInstance::Payload() const noexcept {
        return payload_;
    }

    /** @copydoc UiRuntimeInstance::Activate */
    Result<void> UiRuntimeInstance::Activate() {
        if (state_ != UiRuntimeInstanceState::Prepared)
            return Failure(UiErrors::InstanceStateInvalid);
        state_ = UiRuntimeInstanceState::Active;
        return Result<void>::Success();
    }

    /** @copydoc UiRuntimeInstance::BeginRetirement */
    Result<void> UiRuntimeInstance::BeginRetirement() {
        using enum UiRuntimeInstanceState;
        if (state_ != Prepared && state_ != Active)
            return Failure(UiErrors::InstanceStateInvalid);
        state_ = Retiring;
        return Result<void>::Success();
    }

    /** @copydoc UiRuntimeInstance::Shutdown */
    void UiRuntimeInstance::Shutdown() noexcept {
        state_ = UiRuntimeInstanceState::Stopped;
        std::vector<UiAssetDependency>{}.swap(dependencies_);
        std::vector<std::uint8_t>{}.swap(payload_);
    }
}  // namespace Horo::Runtime::Ui
