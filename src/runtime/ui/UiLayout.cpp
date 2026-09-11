#include "Horo/Runtime/Ui/UiLayout.h"

#include "Horo/Runtime/Ui/UiErrors.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace Horo::Runtime::Ui {
    namespace {
        constexpr std::uint32_t NoParent = std::numeric_limits<std::uint32_t>::max();

        template <typename T = void> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor) {
            return Result<T>::Failure(MakeError(descriptor));
        }

        [[nodiscard]] bool IsDirtyKind(const UiLayoutDirtyKind kind) noexcept {
            return kind >= UiLayoutDirtyKind::Arrange && kind <= UiLayoutDirtyKind::All;
        }

        [[nodiscard]] bool SameOwner(const RuntimeUiInstanceId instance, const UiCanvasInstanceId canvas) noexcept {
            return instance.IsValid() && canvas.IsValid() && instance.ownership == canvas.ownership;
        }
    }  // namespace

    /** @copydoc UiLogicalExtent::IsValid */
    bool UiLogicalExtent::IsValid() const noexcept {
        return width >= 0 && height >= 0;
    }

    /** @copydoc UiLogicalRect::IsValid */
    bool UiLogicalRect::IsValid() const noexcept {
        return extent.IsValid();
    }

    /** @copydoc UiLayoutSourceRevisions::IsValid */
    bool UiLayoutSourceRevisions::IsValid() const noexcept {
        return document.IsValid() && tree.IsValid() && content.IsValid() && style.IsValid() && intrinsic.IsValid() && canvas.IsValid() &&
               policy.IsValid();
    }

    /** @copydoc UiLayoutConstraints::IsValid */
    bool UiLayoutConstraints::IsValid() const noexcept {
        return minimum.IsValid() && maximum.IsValid() && minimum.width <= maximum.width && minimum.height <= maximum.height;
    }

    /** @copydoc UiLayoutMeasurement::IsValid */
    bool UiLayoutMeasurement::IsValid(const UiLayoutConstraints &constraints) const noexcept {
        return constraints.IsValid() && desired.IsValid() && desired.width >= constraints.minimum.width &&
               desired.width <= constraints.maximum.width && desired.height >= constraints.minimum.height &&
               desired.height <= constraints.maximum.height;
    }

    /** @copydoc UiLayoutArrangement::IsValid */
    bool UiLayoutArrangement::IsValid() const noexcept {
        return marginBox.IsValid() && borderBox.IsValid() && paddingBox.IsValid() && contentBox.IsValid() && overflow.IsValid() &&
               hitTest.IsValid() && baseline >= NoUiBaseline;
    }

    /** @copydoc UiLayoutEngineDescriptor::IsValid */
    bool UiLayoutEngineDescriptor::IsValid() const noexcept {
        return SameOwner(instance, canvas) && document.IsValid() && elementCapacity > 0 && elementCapacity <= MaximumUiTreeElements &&
               invalidationCapacity > 0 && invalidationCapacity <= MaximumUiStructuralCommands && concurrentSnapshots >= 2 &&
               concurrentSnapshots <= MaximumUiLayoutSnapshotsInFlight && initialInteractionRevision.IsValid();
    }

    struct UiLayoutSnapshot::Storage final {
        mutable std::atomic<std::uint64_t> leases{};
        UiLayoutSnapshotDescriptor descriptor;
        std::vector<UiLayoutRecord> records;

        explicit Storage(const std::uint32_t capacity) {
            records.reserve(capacity);
        }
    };

    struct UiLayoutEngine::Storage final {
        struct Node final {
            UiElementHandle element;
            std::uint32_t parent{NoParent};
            std::uint32_t firstChild{};
            std::uint32_t childCount{};
            UiLayoutConstraints constraints;
            UiLayoutMeasurement measurement;
            UiLogicalRect assignedContent;
            UiLayoutArrangement arrangement;
            bool measureDirty{true};
            bool arrangeDirty{true};
        };

        UiLayoutEngineDescriptor descriptor;
        UiLayoutEngineState lifecycle{UiLayoutEngineState::Active};
        std::vector<Node> activeNodes;
        std::vector<Node> candidateNodes;
        std::vector<std::uint32_t> activeChildren;
        std::vector<std::uint32_t> candidateChildren;
        std::vector<UiElementHandle> traversalScratch;
        std::vector<UiElementHandle> childHandleScratch;
        std::vector<UiLayoutConstraints> constraintScratch;
        std::vector<UiLayoutChildMeasurement> measurementScratch;
        std::vector<UiLogicalRect> rectangleScratch;
        std::vector<UiLayoutInvalidation> invalidations;
        std::vector<std::shared_ptr<UiLayoutSnapshot::Storage>> slots;
        std::shared_ptr<UiLayoutSnapshot::Storage> current;
        std::size_t nextSlot{};
        UiLayoutSourceRevisions sources;
        UiLayoutConstraints rootConstraints;
        UiLogicalRect rootContent;
        UiInteractionRevision interaction;

        explicit Storage(const UiLayoutEngineDescriptor &source) : descriptor(source), interaction(source.initialInteractionRevision) {
            activeNodes.reserve(source.elementCapacity);
            candidateNodes.reserve(source.elementCapacity);
            activeChildren.reserve(source.elementCapacity - 1);
            candidateChildren.reserve(source.elementCapacity - 1);
            traversalScratch.reserve(source.elementCapacity);
            childHandleScratch.reserve(source.elementCapacity);
            constraintScratch.reserve(source.elementCapacity);
            measurementScratch.reserve(source.elementCapacity);
            rectangleScratch.reserve(source.elementCapacity);
            invalidations.reserve(source.invalidationCapacity);
            slots.reserve(source.concurrentSnapshots);
            for (std::uint32_t index = 0; index < source.concurrentSnapshots; ++index)
                slots.push_back(std::make_shared<UiLayoutSnapshot::Storage>(source.elementCapacity));
        }

        ~Storage() {
            ReleaseCurrent();
        }

        [[nodiscard]] std::uint32_t Find(const std::vector<Node> &nodes, const UiElementHandle element) const noexcept {
            const auto found = std::ranges::find(nodes, element, &Node::element);
            return found == nodes.end() ? NoParent : static_cast<std::uint32_t>(std::distance(nodes.begin(), found));
        }

        [[nodiscard]] Result<void> BuildTopology(const UiElementTree &tree) {
            traversalScratch.resize(descriptor.elementCapacity);
            const auto preorder = tree.Preorder(traversalScratch);
            if (preorder.HasError())
                return Result<void>::Failure(preorder.ErrorValue());
            traversalScratch.resize(preorder.Value());
            if (traversalScratch.empty() || traversalScratch.size() > descriptor.elementCapacity)
                return Failure(UiErrors::CapacityExceeded);

            candidateNodes.clear();
            candidateChildren.clear();
            candidateNodes.resize(traversalScratch.size());
            for (std::uint32_t index = 0; index < traversalScratch.size(); ++index)
                candidateNodes[index].element = traversalScratch[index];

            for (std::uint32_t index = 0; index < candidateNodes.size(); ++index) {
                const auto record = tree.Get(candidateNodes[index].element);
                if (record.HasError())
                    return Result<void>::Failure(record.ErrorValue());
                if (record.Value().parent.IsValid()) {
                    const auto parent = Find(candidateNodes, record.Value().parent);
                    if (parent == NoParent || parent >= index)
                        return Failure(UiErrors::ElementTreeInvalid);
                    candidateNodes[index].parent = parent;
                }

                childHandleScratch.resize(descriptor.elementCapacity);
                const auto children = tree.Children(candidateNodes[index].element, childHandleScratch);
                if (children.HasError())
                    return Result<void>::Failure(children.ErrorValue());
                childHandleScratch.resize(children.Value());
                candidateNodes[index].firstChild = static_cast<std::uint32_t>(candidateChildren.size());
                candidateNodes[index].childCount = static_cast<std::uint32_t>(children.Value());
                for (const auto child : childHandleScratch) {
                    const auto childIndex = Find(candidateNodes, child);
                    if (childIndex == NoParent)
                        return Failure(UiErrors::ElementTreeInvalid);
                    candidateChildren.push_back(childIndex);
                }
            }
            return Result<void>::Success();
        }

        void MarkAll() noexcept {
            for (auto &node : candidateNodes) {
                node.measureDirty = true;
                node.arrangeDirty = true;
            }
        }

        [[nodiscard]] Result<void> ApplyInvalidations(const UiRuntimeTreeRevision treeRevision) {
            for (const auto &invalidation : invalidations) {
                if (!IsDirtyKind(invalidation.kind) || !invalidation.tree.IsValid())
                    return Failure(UiErrors::LayoutInvalid);
                if (invalidation.tree != treeRevision)
                    return Failure(UiErrors::LayoutSourceStale);
                if (invalidation.kind == UiLayoutDirtyKind::All || invalidation.kind == UiLayoutDirtyKind::Structure) {
                    MarkAll();
                    continue;
                }
                auto index = Find(candidateNodes, invalidation.element);
                if (index == NoParent)
                    return Failure(UiErrors::LayoutSourceStale);
                candidateNodes[index].arrangeDirty = true;
                if (invalidation.kind == UiLayoutDirtyKind::Measure) {
                    while (index != NoParent) {
                        candidateNodes[index].measureDirty = true;
                        candidateNodes[index].arrangeDirty = true;
                        index = candidateNodes[index].parent;
                    }
                }
            }
            return Result<void>::Success();
        }

        std::span<const UiElementHandle> ChildHandles(const Node &node) {
            childHandleScratch.resize(node.childCount);
            for (std::uint32_t offset = 0; offset < node.childCount; ++offset)
                childHandleScratch[offset] = candidateNodes[candidateChildren[node.firstChild + offset]].element;
            return {childHandleScratch.data(), node.childCount};
        }

        std::span<const UiLayoutChildMeasurement> ChildMeasurements(const Node &node) {
            measurementScratch.resize(node.childCount);
            for (std::uint32_t offset = 0; offset < node.childCount; ++offset) {
                const auto &child = candidateNodes[candidateChildren[node.firstChild + offset]];
                measurementScratch[offset] = {child.element, child.measurement};
            }
            return {measurementScratch.data(), node.childCount};
        }

        [[nodiscard]] Result<bool> EvaluatePass(const UiLayoutUpdateRequest &request, const bool remeasure) {
            candidateNodes.front().constraints = request.rootConstraints;
            candidateNodes.front().assignedContent = request.rootContent;

            for (std::uint32_t index = 0; index < candidateNodes.size(); ++index) {
                auto &node = candidateNodes[index];
                if (!node.measureDirty || node.childCount == 0)
                    continue;
                constraintScratch.resize(node.childCount);
                const UiLayoutChildConstraintRequest childRequest{node.element, node.constraints, ChildHandles(node)};
                const auto resolved = request.evaluator->ResolveChildConstraints(childRequest, constraintScratch);
                if (resolved.HasError())
                    return Result<bool>::Failure(resolved.ErrorValue());
                for (std::uint32_t offset = 0; offset < node.childCount; ++offset) {
                    if (!constraintScratch[offset].IsValid())
                        return Failure<bool>(UiErrors::LayoutInvalid);
                    auto &child = candidateNodes[candidateChildren[node.firstChild + offset]];
                    if (child.constraints != constraintScratch[offset]) {
                        child.constraints = constraintScratch[offset];
                        child.measureDirty = true;
                        child.arrangeDirty = true;
                    }
                }
            }

            for (std::size_t position = candidateNodes.size(); position > 0; --position) {
                auto &node = candidateNodes[position - 1];
                if (!node.measureDirty)
                    continue;
                const UiLayoutMeasureRequest measureRequest{node.element, node.constraints, ChildMeasurements(node), remeasure};
                const auto measured = request.evaluator->Measure(measureRequest);
                if (measured.HasError())
                    return Result<bool>::Failure(measured.ErrorValue());
                if (!measured.Value().IsValid(node.constraints))
                    return Failure<bool>(UiErrors::LayoutInvalid);
                if (node.measurement != measured.Value())
                    node.arrangeDirty = true;
                node.measurement = measured.Value();
                node.measureDirty = false;
            }

            bool needsRemeasure = false;
            for (std::uint32_t index = 0; index < candidateNodes.size(); ++index) {
                auto &node = candidateNodes[index];
                if (!node.arrangeDirty)
                    continue;
                rectangleScratch.resize(node.childCount);
                const UiLayoutArrangeRequest arrangeRequest{node.element, node.assignedContent, node.measurement, ChildMeasurements(node),
                                                            remeasure};
                const auto arranged = request.evaluator->Arrange(arrangeRequest, rectangleScratch);
                if (arranged.HasError())
                    return Result<bool>::Failure(arranged.ErrorValue());
                if (!arranged.Value().IsValid() || !std::ranges::all_of(rectangleScratch, &UiLogicalRect::IsValid))
                    return Failure<bool>(UiErrors::LayoutInvalid);
                node.arrangement = arranged.Value();
                node.arrangeDirty = false;
                for (std::uint32_t offset = 0; offset < node.childCount; ++offset) {
                    auto &child = candidateNodes[candidateChildren[node.firstChild + offset]];
                    if (child.assignedContent == rectangleScratch[offset])
                        continue;
                    const auto previousAssignment = child.assignedContent;
                    child.assignedContent = rectangleScratch[offset];
                    child.arrangeDirty = true;
                    const bool dependent =
                        (child.measurement.dependsOnParentWidth && previousAssignment.extent.width != child.assignedContent.extent.width) ||
                        (child.measurement.dependsOnParentHeight &&
                         previousAssignment.extent.height != child.assignedContent.extent.height);
                    if (dependent) {
                        auto dirtyIndex = candidateChildren[node.firstChild + offset];
                        while (dirtyIndex != NoParent) {
                            candidateNodes[dirtyIndex].measureDirty = true;
                            candidateNodes[dirtyIndex].arrangeDirty = true;
                            dirtyIndex = candidateNodes[dirtyIndex].parent;
                        }
                        needsRemeasure = true;
                    }
                }
            }
            return Result<bool>::Success(needsRemeasure);
        }

        [[nodiscard]] std::shared_ptr<UiLayoutSnapshot::Storage> TryAcquire() noexcept {
            for (std::size_t offset = 0; offset < slots.size(); ++offset) {
                const auto index = (nextSlot + offset) % slots.size();
                std::uint64_t expected{};
                if (slots[index]->leases.compare_exchange_strong(expected, 1)) {
                    nextSlot = (index + 1) % slots.size();
                    return slots[index];
                }
            }
            return {};
        }

        void ReleaseCurrent() noexcept {
            if (current) {
                current->leases.fetch_sub(1);
                current.reset();
            }
        }
    };

    /** @copydoc UiLayoutSnapshot::UiLayoutSnapshot */
    UiLayoutSnapshot::UiLayoutSnapshot(std::shared_ptr<const Storage> storage) noexcept : storage_(std::move(storage)) {}

    /** @copydoc UiLayoutSnapshot::~UiLayoutSnapshot */
    UiLayoutSnapshot::~UiLayoutSnapshot() {
        Release();
    }

    /** @copydoc UiLayoutSnapshot::UiLayoutSnapshot */
    UiLayoutSnapshot::UiLayoutSnapshot(const UiLayoutSnapshot &other) noexcept : storage_(other.storage_) {
        Retain();
    }

    /** @copydoc UiLayoutSnapshot::operator= */
    UiLayoutSnapshot &UiLayoutSnapshot::operator=(const UiLayoutSnapshot &other) noexcept {
        if (this != &other) {
            Release();
            storage_ = other.storage_;
            Retain();
        }
        return *this;
    }

    /** @copydoc UiLayoutSnapshot::UiLayoutSnapshot */
    UiLayoutSnapshot::UiLayoutSnapshot(UiLayoutSnapshot &&other) noexcept : storage_(std::move(other.storage_)) {}

    /** @copydoc UiLayoutSnapshot::operator= */
    UiLayoutSnapshot &UiLayoutSnapshot::operator=(UiLayoutSnapshot &&other) noexcept {
        if (this != &other) {
            Release();
            storage_ = std::move(other.storage_);
        }
        return *this;
    }

    /** @copydoc UiLayoutSnapshot::Retain */
    void UiLayoutSnapshot::Retain() const noexcept {
        if (storage_)
            storage_->leases.fetch_add(1);
    }

    /** @copydoc UiLayoutSnapshot::Release */
    void UiLayoutSnapshot::Release() noexcept {
        if (storage_) {
            storage_->leases.fetch_sub(1);
            storage_.reset();
        }
    }

    /** @copydoc UiLayoutSnapshot::Descriptor */
    const UiLayoutSnapshotDescriptor &UiLayoutSnapshot::Descriptor() const noexcept {
        return storage_->descriptor;
    }

    /** @copydoc UiLayoutSnapshot::Records */
    std::span<const UiLayoutRecord> UiLayoutSnapshot::Records() const noexcept {
        return storage_->records;
    }

    /** @copydoc UiLayoutSnapshot::Get */
    Result<UiLayoutRecord> UiLayoutSnapshot::Get(const UiElementHandle element) const {
        if (!storage_ || !element.IsValid())
            return Failure<UiLayoutRecord>(UiErrors::LayoutInvalid);
        const auto found = std::ranges::find(storage_->records, element, &UiLayoutRecord::element);
        return found == storage_->records.end() ? Failure<UiLayoutRecord>(UiErrors::HandleStale) : Result<UiLayoutRecord>::Success(*found);
    }

    /** @copydoc UiLayoutEngine::Create */
    Result<UiLayoutEngine> UiLayoutEngine::Create(const UiLayoutEngineDescriptor &descriptor) {
        if (!descriptor.IsValid())
            return Failure<UiLayoutEngine>(UiErrors::LayoutInvalid);
        try {
            return Result<UiLayoutEngine>::Success(UiLayoutEngine{std::make_unique<Storage>(descriptor)});
        } catch (const std::bad_alloc &) {
            return Failure<UiLayoutEngine>(UiErrors::CapacityExceeded);
        }
    }

    /** @copydoc UiLayoutEngine::UiLayoutEngine */
    UiLayoutEngine::UiLayoutEngine(std::unique_ptr<Storage> storage) noexcept : storage_(std::move(storage)) {}

    /** @copydoc UiLayoutEngine::~UiLayoutEngine */
    UiLayoutEngine::~UiLayoutEngine() {
        Shutdown();
    }

    /** @copydoc UiLayoutEngine::UiLayoutEngine */
    UiLayoutEngine::UiLayoutEngine(UiLayoutEngine &&) noexcept = default;

    /** @copydoc UiLayoutEngine::operator= */
    UiLayoutEngine &UiLayoutEngine::operator=(UiLayoutEngine &&) noexcept = default;

    /** @copydoc UiLayoutEngine::Invalidate */
    Result<void> UiLayoutEngine::Invalidate(const UiLayoutInvalidation invalidation) {
        if (!storage_ || storage_->lifecycle != UiLayoutEngineState::Active)
            return Failure(UiErrors::LayoutLifecycleUnavailable);
        if (!invalidation.tree.IsValid() || !IsDirtyKind(invalidation.kind) ||
            (invalidation.kind != UiLayoutDirtyKind::All && !invalidation.element.IsValid()))
            return Failure(UiErrors::LayoutInvalid);
        const auto all = std::ranges::find(storage_->invalidations, UiLayoutDirtyKind::All, &UiLayoutInvalidation::kind);
        if (all != storage_->invalidations.end())
            return Result<void>::Success();
        if (invalidation.kind == UiLayoutDirtyKind::All) {
            storage_->invalidations.clear();
            storage_->invalidations.push_back(invalidation);
            return Result<void>::Success();
        }
        const auto existing = std::ranges::find_if(storage_->invalidations, [&invalidation](const UiLayoutInvalidation &queued) {
            return queued.tree == invalidation.tree && queued.element == invalidation.element;
        });
        if (existing != storage_->invalidations.end()) {
            existing->kind = std::max(existing->kind, invalidation.kind);
            return Result<void>::Success();
        }
        if (storage_->invalidations.size() == storage_->descriptor.invalidationCapacity)
            return Failure(UiErrors::CapacityExceeded);
        storage_->invalidations.push_back(invalidation);
        return Result<void>::Success();
    }

    /** @copydoc UiLayoutEngine::Update */
    Result<UiLayoutSnapshot> UiLayoutEngine::Update(const UiElementTree &tree, const UiLayoutUpdateRequest &request) {
        if (!storage_ || storage_->lifecycle != UiLayoutEngineState::Active)
            return Failure<UiLayoutSnapshot>(UiErrors::LayoutLifecycleUnavailable);
        if (!request.sources.IsValid() || !request.rootConstraints.IsValid() || !request.rootContent.IsValid() ||
            request.evaluator == nullptr)
            return Failure<UiLayoutSnapshot>(UiErrors::LayoutInvalid);
        if (tree.State() != UiElementTreeState::Active || tree.Instance() != storage_->descriptor.instance ||
            tree.Canvas() != storage_->descriptor.canvas || tree.SourceDocument() != storage_->descriptor.document ||
            tree.SourceDocumentRevision() != request.sources.document || tree.Revision() != request.sources.tree ||
            tree.Size() > storage_->descriptor.elementCapacity)
            return Failure<UiLayoutSnapshot>(UiErrors::LayoutSourceStale);

        const bool topologyChanged = storage_->activeNodes.empty() || storage_->sources.tree != request.sources.tree;
        const bool rootChanged =
            storage_->current && (storage_->rootConstraints != request.rootConstraints || storage_->rootContent != request.rootContent);
        const bool sourcesChanged = !storage_->current || storage_->sources != request.sources ||
                                    storage_->rootConstraints != request.rootConstraints || storage_->rootContent != request.rootContent;
        if (!sourcesChanged && storage_->invalidations.empty()) {
            storage_->current->leases.fetch_add(1);
            return Result<UiLayoutSnapshot>::Success(UiLayoutSnapshot{storage_->current});
        }

        if (topologyChanged) {
            const auto topology = storage_->BuildTopology(tree);
            if (topology.HasError())
                return Result<UiLayoutSnapshot>::Failure(topology.ErrorValue());
            storage_->MarkAll();
        } else {
            storage_->candidateNodes = storage_->activeNodes;
            storage_->candidateChildren = storage_->activeChildren;
        }
        if (rootChanged || (sourcesChanged && storage_->invalidations.empty()))
            storage_->MarkAll();
        const auto invalidated = storage_->ApplyInvalidations(request.sources.tree);
        if (invalidated.HasError())
            return Result<UiLayoutSnapshot>::Failure(invalidated.ErrorValue());

        const auto firstPass = storage_->EvaluatePass(request, false);
        if (firstPass.HasError())
            return Result<UiLayoutSnapshot>::Failure(firstPass.ErrorValue());
        if (firstPass.Value()) {
            const auto secondPass = storage_->EvaluatePass(request, true);
            if (secondPass.HasError())
                return Result<UiLayoutSnapshot>::Failure(secondPass.ErrorValue());
            if (secondPass.Value())
                return Failure<UiLayoutSnapshot>(UiErrors::LayoutNonConvergent);
        }

        auto slot = storage_->TryAcquire();
        if (!slot)
            return Failure<UiLayoutSnapshot>(UiErrors::LayoutSnapshotStorageExhausted);
        UiInteractionRevision publication = storage_->interaction;
        if (storage_->current) {
            const auto next = storage_->interaction.Next();
            if (next.HasError()) {
                slot->leases.store(0);
                return Result<UiLayoutSnapshot>::Failure(next.ErrorValue());
            }
            publication = next.Value();
        }
        slot->descriptor = {storage_->descriptor.instance, storage_->descriptor.canvas, storage_->descriptor.document, request.sources,
                            publication};
        slot->records.resize(storage_->candidateNodes.size());
        for (std::size_t index = 0; index < storage_->candidateNodes.size(); ++index) {
            const auto &node = storage_->candidateNodes[index];
            slot->records[index] = {node.element, node.measurement, node.arrangement};
        }

        storage_->ReleaseCurrent();
        storage_->current = slot;
        storage_->activeNodes.swap(storage_->candidateNodes);
        storage_->activeChildren.swap(storage_->candidateChildren);
        storage_->sources = request.sources;
        storage_->rootConstraints = request.rootConstraints;
        storage_->rootContent = request.rootContent;
        storage_->interaction = publication;
        storage_->invalidations.clear();
        slot->leases.fetch_add(1);
        return Result<UiLayoutSnapshot>::Success(UiLayoutSnapshot{std::move(slot)});
    }

    /** @copydoc UiLayoutEngine::BeginRetirement */
    Result<void> UiLayoutEngine::BeginRetirement() {
        if (!storage_ || storage_->lifecycle != UiLayoutEngineState::Active)
            return Failure(UiErrors::LayoutLifecycleUnavailable);
        storage_->lifecycle = UiLayoutEngineState::Retiring;
        storage_->invalidations.clear();
        return Result<void>::Success();
    }

    /** @copydoc UiLayoutEngine::Shutdown */
    void UiLayoutEngine::Shutdown() noexcept {
        if (!storage_ || storage_->lifecycle == UiLayoutEngineState::Stopped)
            return;
        storage_->lifecycle = UiLayoutEngineState::Stopped;
        storage_->invalidations.clear();
        storage_->activeNodes.clear();
        storage_->candidateNodes.clear();
        storage_->activeChildren.clear();
        storage_->candidateChildren.clear();
        storage_->ReleaseCurrent();
    }

    /** @copydoc UiLayoutEngine::State */
    UiLayoutEngineState UiLayoutEngine::State() const noexcept {
        return storage_ ? storage_->lifecycle : UiLayoutEngineState::Stopped;
    }

    /** @copydoc UiLayoutEngine::IsDrained */
    bool UiLayoutEngine::IsDrained() const noexcept {
        if (!storage_)
            return true;
        return std::ranges::all_of(storage_->slots, [&storage = *storage_](const auto &slot) {
            const auto leases = slot->leases.load();
            return leases == 0 || (slot == storage.current && leases == 1);
        });
    }
}  // namespace Horo::Runtime::Ui
