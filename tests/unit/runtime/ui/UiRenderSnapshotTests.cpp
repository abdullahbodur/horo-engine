#include "Horo/Runtime/Ui/UiRenderSnapshot.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <vector>

namespace Horo::Runtime::Ui {
    namespace {
        SerializedUiId StableBytes(const std::uint8_t marker) {
            SerializedUiId bytes{};
            bytes.back() = marker;
            return bytes;
        }

        UiDocumentId Document(const std::uint8_t marker) {
            return UiDocumentId::Create(StableBytes(marker)).Value();
        }

        UiElementId Element(const std::uint8_t marker) {
            return UiElementId::Create(StableBytes(marker)).Value();
        }

        UiOwnershipGeneration Owner(const std::uint64_t value = 17) {
            return UiOwnershipGeneration::Create(value).Value();
        }

        UiDocumentRevision DocumentRevision(const std::uint64_t value = 4) {
            return UiDocumentRevision::Create(value).Value();
        }

        UiRuntimeTreeRevision TreeRevision(const std::uint64_t value = 6) {
            return UiRuntimeTreeRevision::Create(value).Value();
        }

        UiInteractionRevision InteractionRevision(const std::uint64_t value = 8) {
            return UiInteractionRevision::Create(value).Value();
        }

        UiRenderSnapshotRevision SnapshotRevision(const std::uint64_t value = 2) {
            return UiRenderSnapshotRevision::Create(value).Value();
        }

        UiRenderResourceRevision ResourceRevision(const std::uint64_t value = 3) {
            return UiRenderResourceRevision::Create(value).Value();
        }

        Assets::AssetId Asset(const std::uint8_t marker) {
            std::array<std::uint8_t, 16> bytes{};
            bytes.back() = marker;
            return Assets::AssetId::FromBytes(bytes);
        }

        UiElementTree Tree() {
            const UiElementTreeDescriptor descriptor{.instance = {Owner(), 1, 1},
                                                     .canvas = {Owner(), 2, 1},
                                                     .document = Document(1),
                                                     .documentRevision = DocumentRevision(),
                                                     .treeRevision = TreeRevision(),
                                                     .limits = {4, 4, 4}};
            const std::array elements{UiElementDescriptor{Element(2), {}}, UiElementDescriptor{Element(3), Element(2)}};
            auto result = UiElementTree::Create(descriptor, elements);
            REQUIRE(result.HasValue());
            return std::move(result).Value();
        }

        UiRenderSnapshotLimits Limits() {
            return {8, 4, 16, 4, 4, 4, 4};
        }

        UiRenderSnapshotDescriptor Descriptor(const UiElementTree &tree) {
            return {.instance = tree.Instance(),
                    .canvas = tree.Canvas(),
                    .document = tree.SourceDocument(),
                    .documentRevision = tree.SourceDocumentRevision(),
                    .treeRevision = tree.Revision(),
                    .interactionRevision = InteractionRevision(),
                    .snapshotRevision = SnapshotRevision(),
                    .view = {Owner(), 9, 1},
                    .limits = Limits()};
        }

        struct Projection final {
            std::vector<UiDrawCommand> commands;
            std::vector<UiTextRun> textRuns;
            std::vector<UiPositionedGlyph> glyphs;
            std::vector<UiClip> clips;
            std::vector<UiMask> masks;
            std::vector<UiLogicalTransform> transforms;
            std::vector<UiRenderResourceReference> resources;
        };

        Projection CompleteProjection(const UiElementTree &tree) {
            Projection result;
            result.resources = {{Asset(1), ResourceRevision(), UiRenderResourceRole::Image},
                                {Asset(2), ResourceRevision(), UiRenderResourceRole::FontFace},
                                {Asset(3), ResourceRevision(), UiRenderResourceRole::Mask}};
            result.transforms = {{}, {{1.0F, 0.0F, 0.0F, 1.0F, 64.0F, 32.0F}}};
            result.clips = {{{{0, 0}, {640, 320}}, NoUiRenderIndex}, {{{64, 32}, {320, 160}}, 0}};
            result.masks = {{{{0, 0}, {640, 320}}, 2, 0}};
            result.glyphs = {{17, 0, {64, 64}}, {18, 1, {128, 64}}};
            result.textRuns = {{1, 0, 2, {1.0F, 1.0F, 1.0F, 1.0F}}};
            const auto root = tree.Root().Value().handle;
            const auto child = tree.Find(Element(3)).Value();
            result.commands = {{root, {{0, 0}, {640, 320}}, 0, 0, NoUiRenderIndex, 1.0F, UiSolidDraw{{0.1F, 0.2F, 0.3F, 1.0F}}},
                               {root, {{0, 0}, {640, 320}}, 0, 0, NoUiRenderIndex, 1.0F, UiBorderDraw{{1.0F, 1.0F, 1.0F, 1.0F}, 2}},
                               {child, {{64, 32}, {128, 64}}, 1, 1, 0, 0.75F, UiImageDraw{0, {1.0F, 1.0F, 1.0F, 1.0F}}},
                               {child, {{64, 96}, {256, 64}}, 0, 1, NoUiRenderIndex, 1.0F, UiTextDraw{0}}};
            return result;
        }

        Result<UiRenderSnapshot> Extract(const UiElementTree &tree, const UiRenderSnapshotDescriptor &descriptor,
                                         const Projection &projection) {
            return UiRenderSnapshot::Extract(tree, descriptor,
                                             {projection.commands, projection.textRuns, projection.glyphs, projection.clips,
                                              projection.masks, projection.transforms, projection.resources});
        }

        TEST_CASE("UI render extraction owns complete stable paint-order values", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            auto projection = CompleteProjection(tree);
            auto extracted = Extract(tree, Descriptor(tree), projection);
            REQUIRE(extracted.HasValue());
            auto snapshot = std::move(extracted).Value();
            REQUIRE(snapshot.Commands().size() == 4);
            REQUIRE(std::holds_alternative<UiSolidDraw>(snapshot.Commands()[0].payload));
            REQUIRE(std::holds_alternative<UiBorderDraw>(snapshot.Commands()[1].payload));
            REQUIRE(std::holds_alternative<UiImageDraw>(snapshot.Commands()[2].payload));
            REQUIRE(std::holds_alternative<UiTextDraw>(snapshot.Commands()[3].payload));
            REQUIRE(snapshot.TextRuns()[0].glyphCount == 2);
            REQUIRE(snapshot.Clips()[1].parent == 0);
            REQUIRE(snapshot.Resources()[1].role == UiRenderResourceRole::FontFace);

            projection.commands.clear();
            projection.glyphs[0].glyph = 999;
            tree.Shutdown();
            REQUIRE(snapshot.Commands().size() == 4);
            REQUIRE(snapshot.Glyphs()[0].glyph == 17);
            REQUIRE(snapshot.Descriptor().snapshotRevision == SnapshotRevision());
        }

        TEST_CASE("UI render extraction requires exact live tree and view evidence", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            const auto projection = CompleteProjection(tree);
            auto descriptor = Descriptor(tree);
            descriptor.instance.slot = 99;
            REQUIRE(Extract(tree, descriptor, projection).HasError());
            descriptor = Descriptor(tree);
            descriptor.documentRevision = DocumentRevision(99);
            REQUIRE(Extract(tree, descriptor, projection).HasError());
            descriptor = Descriptor(tree);
            descriptor.interactionRevision = {};
            REQUIRE(Extract(tree, descriptor, projection).HasError());
            descriptor = Descriptor(tree);
            descriptor.view.ownership = Owner(18);
            REQUIRE(Extract(tree, descriptor, projection).HasError());
            REQUIRE(tree.BeginRetirement().HasValue());
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
        }

        TEST_CASE("UI render extraction validates resource and text ranges", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            auto projection = CompleteProjection(tree);
            projection.resources[0].asset = {};
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.resources[0].role = static_cast<UiRenderResourceRole>(99);
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.textRuns[0].fontResource = 0;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.textRuns[0].firstGlyph = std::numeric_limits<std::uint32_t>::max();
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.textRuns[0].color.red = -0.1F;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
        }

        TEST_CASE("UI render extraction validates clips masks and finite transforms", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            auto projection = CompleteProjection(tree);
            projection.clips[0].parent = 1;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.clips[0].rect.extent.width = -1;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.masks[0].resource = 0;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.masks[0].transform = 99;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.transforms[0].values[0] = std::numeric_limits<float>::infinity();
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
        }

        TEST_CASE("UI render extraction validates every draw reference and paint value", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            auto projection = CompleteProjection(tree);
            projection.commands[0].element.ownership = Owner(19);
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.commands[0].transform = 99;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            projection.commands[0].opacity = 2.0F;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            std::get<UiBorderDraw>(projection.commands[1].payload).width = -1;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            std::get<UiImageDraw>(projection.commands[2].payload).resource = 1;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
            projection = CompleteProjection(tree);
            std::get<UiTextDraw>(projection.commands[3].payload).run = 99;
            REQUIRE(Extract(tree, Descriptor(tree), projection).HasError());
        }

        TEST_CASE("UI render extraction enforces declared bounds and permits an empty view", "[runtime_ui][render_snapshot]") {
            auto tree = Tree();
            auto projection = CompleteProjection(tree);
            auto descriptor = Descriptor(tree);
            descriptor.limits.commands = static_cast<std::uint32_t>(projection.commands.size());
            REQUIRE(Extract(tree, descriptor, projection).HasValue());
            --descriptor.limits.commands;
            REQUIRE(Extract(tree, descriptor, projection).HasError());
            descriptor = Descriptor(tree);
            descriptor.limits.transforms = 0;
            REQUIRE(Extract(tree, descriptor, projection).HasError());

            Projection empty;
            REQUIRE(Extract(tree, Descriptor(tree), empty).HasValue());
            REQUIRE(UiRenderSnapshot::Extract(tree, Descriptor(tree), {}).HasValue());
        }

        static_assert(std::is_copy_constructible_v<UiRenderSnapshot>);
    }  // namespace
}  // namespace Horo::Runtime::Ui
