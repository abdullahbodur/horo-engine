#pragma once

namespace Monolith {

// Zero-size tag added to the player entity.
// Behaviors locate the player via registry.GetEntities<PlayerTagComponent>(); always
// guard against empty before indexing (there may be zero players in editor mode).
struct PlayerTagComponent {};

}  // namespace Monolith
