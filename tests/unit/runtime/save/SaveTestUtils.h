#pragma once

#include "Horo/Runtime/Save/SaveArchiveMetadata.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Horo::Runtime::Test {
    template <typename Identity> Identity Id(const std::uint8_t suffix) {
        std::array<std::uint8_t, 16> bytes{};
        bytes.back() = suffix;
        return Identity::FromBytes(bytes).Value();
    }

    template <typename Version> Version V(const std::uint32_t value) {
        return Version::Create(value).Value();
    }

    inline std::vector<SaveManifestParticipant> StandardParticipants(const std::uint32_t sceneVersion,
                                                                     const std::uint32_t gameplayVersion) {
        return {{.participant = SaveParticipantId::Parse("horo.scene.core.v1").Value(),
                 .schemaVersion = V<ParticipantSchemaVersion>(sceneVersion),
                 .required = true,
                 .chunks = {Id<SaveRecordId>(20), Id<SaveRecordId>(21)}},
                {.participant = SaveParticipantId::Parse("project.gameplay.v1").Value(),
                 .schemaVersion = V<ParticipantSchemaVersion>(gameplayVersion),
                 .required = false,
                 .chunks = {Id<SaveRecordId>(22)}}};
    }
}  // namespace Horo::Runtime::Test
