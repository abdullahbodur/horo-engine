#include "Horo/Runtime/Ui/UiElementTree.h"

#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
    std::atomic<std::size_t> allocations{};
}

void *operator new(const std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (void *memory = std::malloc(size))
        return memory;
    throw std::bad_alloc{};
}

void operator delete(void *memory) noexcept {
    std::free(memory);
}

void operator delete(void *memory, std::size_t) noexcept {
    std::free(memory);
}

namespace Horo::Runtime::Ui {
    namespace {
        template <typename Id> Id Stable(const std::uint8_t marker) {
            SerializedUiId bytes{};
            bytes.back() = marker;
            return Id::Create(bytes).Value();
        }

        UiOwnershipGeneration Owner(const std::uint64_t value = 7) {
            return UiOwnershipGeneration::Create(value).Value();
        }

        RuntimeUiInstanceId Instance(const UiOwnershipGeneration owner = Owner()) {
            return {owner, 1, 1};
        }

        UiCanvasInstanceId Canvas(const UiOwnershipGeneration owner = Owner()) {
            return {owner, 2, 1};
        }

        UiDocumentRevision DocumentRevision(const std::uint64_t value = 3) {
            return UiDocumentRevision::Create(value).Value();
        }

        UiRuntimeTreeRevision TreeRevision(const std::uint64_t value = 5) {
            return UiRuntimeTreeRevision::Create(value).Value();
        }

        UiElementTreeDescriptor Descriptor(UiElementTreeLimits limits = {8, 4, 8}) {
            return {.instance = Instance(),
                    .canvas = Canvas(),
                    .document = Stable<UiDocumentId>(20),
                    .documentRevision = DocumentRevision(),
                    .treeRevision = TreeRevision(),
                    .limits = limits};
        }

        std::vector<UiElementDescriptor> Elements() {
            return {{Stable<UiElementId>(1), {}},
                    {Stable<UiElementId>(2), Stable<UiElementId>(1)},
                    {Stable<UiElementId>(3), Stable<UiElementId>(1)},
                    {Stable<UiElementId>(4), Stable<UiElementId>(2)}};
        }

        UiElementTree Tree(UiElementTreeDescriptor descriptor = Descriptor()) {
            const auto elements = Elements();
            auto result = UiElementTree::Create(descriptor, elements);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        UiStructuralCommandBuffer Commands(const UiElementTree &tree, const std::uint32_t capacity = 8) {
            auto result = UiStructuralCommandBuffer::Create(tree.Instance(), tree.Canvas(), DocumentRevision(), tree.Revision(), capacity);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        void Add(UiStructuralCommandBuffer &buffer, UiStructuralCommand command) {
            REQUIRE(buffer.Add(std::move(command)).HasValue());
        }

        std::vector<UiElementId> PreorderIds(const UiElementTree &tree) {
            std::vector<UiElementHandle> handles(tree.Size());
            REQUIRE(tree.Preorder(handles).HasValue());
            std::vector<UiElementId> ids;
            for (const auto handle : handles)
                ids.push_back(tree.Get(handle).Value().id);
            return ids;
        }

        TEST_CASE("Retained UI tree owns deterministic authored child and preorder order", "[runtime_ui][tree]") {
            const auto tree = Tree();
            REQUIRE(tree.Size() == 4);
            REQUIRE(tree.SourceDocument() == Stable<UiDocumentId>(20));
            REQUIRE(tree.SourceDocumentRevision() == DocumentRevision());
            REQUIRE(tree.Root().Value().id == Stable<UiElementId>(1));
            REQUIRE(PreorderIds(tree) ==
                    std::vector{Stable<UiElementId>(1), Stable<UiElementId>(2), Stable<UiElementId>(4), Stable<UiElementId>(3)});

            std::array<UiElementHandle, 2> children{};
            const auto root = tree.Root().Value().handle;
            REQUIRE(tree.Children(root, children).Value() == 2);
            REQUIRE(tree.Get(children[0]).Value().id == Stable<UiElementId>(2));
            REQUIRE(tree.Get(children[1]).Value().id == Stable<UiElementId>(3));
            REQUIRE(tree.Get(children[0]).Value().parent == root);
        }

        TEST_CASE("Retained UI tree rejects malformed roots parents and identities transactionally", "[runtime_ui][tree]") {
            auto descriptor = Descriptor();
            REQUIRE(UiElementTree::Create(descriptor, {}).HasError());
            descriptor.canvas = Canvas(Owner(8));
            REQUIRE(UiElementTree::Create(descriptor, Elements()).HasError());

            auto elements = Elements();
            elements[1].id = elements[0].id;
            REQUIRE(UiElementTree::Create(Descriptor(), elements).HasError());
            elements = Elements();
            elements[1].parent = Stable<UiElementId>(99);
            REQUIRE(UiElementTree::Create(Descriptor(), elements).HasError());
            elements = Elements();
            elements[1].parent = {};
            REQUIRE(UiElementTree::Create(Descriptor(), elements).HasError());
        }

        TEST_CASE("Retained UI tree rejects cycles depth overflow and unsupported limits", "[runtime_ui][tree]") {
            auto elements = Elements();
            elements[0].parent = elements[3].id;
            REQUIRE(UiElementTree::Create(Descriptor(), elements).HasError());
            REQUIRE(UiElementTree::Create(Descriptor({4, 2, 4}), Elements()).HasError());
            REQUIRE(UiElementTree::Create(Descriptor({4, 3, 4}), Elements()).HasValue());
            REQUIRE(UiElementTree::Create(Descriptor({3, 3, 4}), Elements()).HasError());
            REQUIRE_FALSE(UiElementTreeLimits{0, 1, 1}.IsValid());
            REQUIRE_FALSE(UiElementTreeLimits{1, MaximumUiTreeDepth + 1, 1}.IsValid());
        }

        TEST_CASE("Deferred UI insertion publishes only at a declared safe point", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            const auto before = tree.Revision();
            const auto parent = tree.Find(Stable<UiElementId>(1)).Value();
            auto buffer = Commands(tree);
            Add(buffer, UiInsertElementCommand{Stable<UiElementId>(5), parent, 1});
            REQUIRE(tree.Find(Stable<UiElementId>(5)).HasError());
            REQUIRE(tree.Revision() == before);
            const auto committed = tree.CommitDeferred(buffer, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands);
            REQUIRE(committed.HasValue());
            REQUIRE(committed.Value().commandsApplied == 1);
            REQUIRE(committed.Value().revision == before.Next().Value());
            REQUIRE(tree.Find(Stable<UiElementId>(5)).HasValue());
            REQUIRE(PreorderIds(tree) == std::vector{Stable<UiElementId>(1), Stable<UiElementId>(2), Stable<UiElementId>(4),
                                                     Stable<UiElementId>(5), Stable<UiElementId>(3)});
        }

        TEST_CASE("Deferred UI commands reparent reorder and remove subtrees in buffer order", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            const auto two = tree.Find(Stable<UiElementId>(2)).Value();
            const auto three = tree.Find(Stable<UiElementId>(3)).Value();
            const auto four = tree.Find(Stable<UiElementId>(4)).Value();
            auto buffer = Commands(tree);
            Add(buffer, UiReparentElementCommand{four, three, 0});
            Add(buffer, UiRemoveElementCommand{two});
            const auto committed = tree.CommitDeferred(buffer, UiStructuralCommitPoint::CommitDeferredLifecycleChanges);
            REQUIRE(committed.HasValue());
            REQUIRE(committed.Value().elementsRemoved == 1);
            REQUIRE(PreorderIds(tree) == std::vector{Stable<UiElementId>(1), Stable<UiElementId>(3), Stable<UiElementId>(4)});
            REQUIRE(tree.Get(two).HasError());
            REQUIRE(tree.Get(four).Value().parent == three);
        }

        TEST_CASE("Deferred UI command failures preserve revision handles and topology", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            const auto revision = tree.Revision();
            const auto original = PreorderIds(tree);
            const auto two = tree.Find(Stable<UiElementId>(2)).Value();
            const auto four = tree.Find(Stable<UiElementId>(4)).Value();
            auto buffer = Commands(tree);
            Add(buffer, UiInsertElementCommand{Stable<UiElementId>(5), two, 0});
            Add(buffer, UiReparentElementCommand{two, four, 0});
            REQUIRE(tree.CommitDeferred(buffer, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasError());
            REQUIRE(tree.Revision() == revision);
            REQUIRE(PreorderIds(tree) == original);
            REQUIRE(tree.Find(Stable<UiElementId>(5)).HasError());
            REQUIRE(tree.Get(two).HasValue());
        }

        TEST_CASE("Deferred UI commands reject root stale foreign and invalid child requests", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            const auto root = tree.Root().Value().handle;
            auto removeRoot = Commands(tree);
            Add(removeRoot, UiRemoveElementCommand{root});
            REQUIRE(tree.CommitDeferred(removeRoot, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasError());

            auto invalidIndex = Commands(tree);
            Add(invalidIndex, UiInsertElementCommand{Stable<UiElementId>(5), root, 99});
            REQUIRE(tree.CommitDeferred(invalidIndex, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasError());

            auto foreign = Commands(tree);
            Add(foreign, UiRemoveElementCommand{{Owner(9), root.slot, root.generation}});
            REQUIRE(tree.CommitDeferred(foreign, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasError());
            REQUIRE(tree.Size() == 4);
        }

        TEST_CASE("Removed UI slots reuse a fresh generation and stale handles stay rejected", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            const auto removed = tree.Find(Stable<UiElementId>(2)).Value();
            auto remove = Commands(tree);
            Add(remove, UiRemoveElementCommand{removed});
            REQUIRE(tree.CommitDeferred(remove, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasValue());
            REQUIRE(tree.Get(removed).HasError());

            auto insert = Commands(tree);
            Add(insert, UiInsertElementCommand{Stable<UiElementId>(5), tree.Root().Value().handle, 0});
            REQUIRE(tree.CommitDeferred(insert, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasValue());
            const auto replacement = tree.Find(Stable<UiElementId>(5)).Value();
            REQUIRE(replacement.slot == removed.slot);
            REQUIRE(replacement.generation != removed.generation);
            REQUIRE(tree.Get(removed).HasError());
        }

        TEST_CASE("Deferred UI transaction identity revision and capacity are exact", "[runtime_ui][tree][commands]") {
            auto tree = Tree();
            auto full = Commands(tree, 1);
            Add(full, UiInsertElementCommand{Stable<UiElementId>(5), tree.Root().Value().handle, 0});
            REQUIRE(full.Add(UiRemoveElementCommand{tree.Find(Stable<UiElementId>(2)).Value()}).HasError());

            auto empty = Commands(tree);
            const auto noOp = tree.CommitDeferred(empty, UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands);
            REQUIRE(noOp.HasValue());
            REQUIRE(noOp.Value().revision == tree.Revision());
            REQUIRE(noOp.Value().commandsApplied == 0);

            auto stale = UiStructuralCommandBuffer::Create(tree.Instance(), tree.Canvas(), DocumentRevision(), TreeRevision(4), 1);
            REQUIRE(stale.HasValue());
            REQUIRE(tree.CommitDeferred(stale.Value(), UiStructuralCommitPoint::ApplyQueuedOwnerThreadCommands).HasError());
            REQUIRE(UiStructuralCommandBuffer::Create({}, tree.Canvas(), DocumentRevision(), tree.Revision(), 1).HasError());
            REQUIRE(UiStructuralCommandBuffer::Create(tree.Instance(), tree.Canvas(), {}, tree.Revision(), 1).HasError());
        }

        TEST_CASE("Retained UI queries and bounded traversal allocate no memory", "[runtime_ui][tree][realtime]") {
            const auto tree = Tree();
            std::array<UiElementHandle, 4> preorder{};
            std::array<UiElementHandle, 2> children{};
            const auto root = tree.Root().Value().handle;
            const auto before = allocations.load(std::memory_order_relaxed);
            const auto found = tree.Find(Stable<UiElementId>(4));
            const auto record = tree.Get(found.Value());
            const auto childCount = tree.Children(root, children);
            const auto traversalCount = tree.Preorder(preorder);
            const auto after = allocations.load(std::memory_order_relaxed);
            REQUIRE(after == before);
            REQUIRE(record.HasValue());
            REQUIRE(childCount.Value() == 2);
            REQUIRE(traversalCount.Value() == 4);
        }

        TEST_CASE("Retained UI retirement closes commits and shutdown is idempotent", "[runtime_ui][tree][shutdown]") {
            auto tree = Tree();
            auto pending = Commands(tree);
            Add(pending, UiInsertElementCommand{Stable<UiElementId>(5), tree.Root().Value().handle, 0});
            REQUIRE(tree.BeginRetirement().HasValue());
            REQUIRE(tree.State() == UiElementTreeState::Retiring);
            REQUIRE(tree.Root().HasValue());
            REQUIRE(tree.CommitDeferred(pending, UiStructuralCommitPoint::CommitDeferredLifecycleChanges).HasError());
            REQUIRE(tree.BeginRetirement().HasError());
            tree.Shutdown();
            tree.Shutdown();
            REQUIRE(tree.State() == UiElementTreeState::Stopped);
            REQUIRE(tree.Size() == 0);
            REQUIRE(tree.Root().HasError());
        }

        TEST_CASE("Runtime replacement keeps stable IDs but issues new runtime handles", "[runtime_ui][tree][reload]") {
            const auto oldTree = Tree();
            auto replacementDescriptor = Descriptor();
            replacementDescriptor.instance = {Owner(8), 1, 1};
            replacementDescriptor.canvas = {Owner(8), 2, 1};
            replacementDescriptor.treeRevision = TreeRevision(1);
            const auto replacement = Tree(replacementDescriptor);
            REQUIRE(oldTree.Root().Value().id == replacement.Root().Value().id);
            REQUIRE(oldTree.Root().Value().handle != replacement.Root().Value().handle);
            REQUIRE(oldTree.Get(replacement.Root().Value().handle).HasError());
            auto invalid = Elements();
            invalid[1].parent = Stable<UiElementId>(99);
            REQUIRE(UiElementTree::Create(replacementDescriptor, invalid).HasError());
            REQUIRE(oldTree.Size() == 4);
            REQUIRE(oldTree.Root().HasValue());
        }

        static_assert(!std::is_copy_constructible_v<UiElementTree>);
    }  // namespace
}  // namespace Horo::Runtime::Ui
