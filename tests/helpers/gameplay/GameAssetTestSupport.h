#pragma once

#include "Horo/Gameplay/GameAsset.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Horo::Tests {
    inline Gameplay::GameAssetTypeDescriptor QuestGameAssetDescriptor(const std::uint32_t schemaVersion = 1,
                                                                      std::vector<std::string> sourceExtensions = {"quest"},
                                                                      std::vector<AssetCookTargetId> cookTargets = {
                                                                          AssetCookTargetId::Parse("headless-null").Value()}) {
        return {
            .typeId = Gameplay::GameAssetTypeId::Parse("game.tests.quest_definition").Value(),
            .schemaVersion = schemaVersion,
            .sourceExtensions = std::move(sourceExtensions),
            .cookTargets = std::move(cookTargets),
            .editor =
                {
                    .displayName = "Quest Definition",
                    .category = "Gameplay/Quests",
                    .iconName = "asset-quest",
                    .fields = {{Gameplay::GameAssetFieldId::Parse("title").Value(), "Title", Gameplay::GameAssetFieldKind::String, true}},
                },
        };
    }
}  // namespace Horo::Tests
