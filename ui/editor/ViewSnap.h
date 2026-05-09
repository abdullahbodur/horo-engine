/** @file ViewSnap.h
 *  @brief Enum representing orthographic camera snap directions for the editor viewport.
 */
#pragma once

namespace Horo::Editor {

/** @brief Canonical camera snap directions for aligning the viewport to a world axis. */
enum class ViewSnap {
    None,   /**< No snap; free-orbit mode. */
    Top,    /**< Snap to a top-down view (looking down the -Y axis). */
    Bottom, /**< Snap to a bottom-up view (looking up the +Y axis). */
    Left,   /**< Snap to a left-side view (looking along the +X axis). */
    Right,  /**< Snap to a right-side view (looking along the -X axis). */
    Front,  /**< Snap to a front view (looking along the +Z axis). */
    Back,   /**< Snap to a back view (looking along the -Z axis). */
};

}
