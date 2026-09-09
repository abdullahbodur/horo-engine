#pragma once

namespace Horo::Editor {
    /** @brief One frame of normalized horizontal and vertical editor wheel input. */
    struct EditorScrollDelta final {
        float horizontal{};
        float vertical{};

        [[nodiscard]] bool IsEmpty() const noexcept;
    };

    /** @brief Bounds discrete wheel input per frame while preserving high-resolution deltas. */
    class EditorScrollSmoother final {
    public:
        /**
         * @brief Adds normalized wheel input, replacing stale momentum when an axis reverses.
         * @param horizontal Horizontal wheel units; non-finite values are ignored.
         * @param vertical Vertical wheel units; non-finite values are ignored.
         */
        void Queue(float horizontal, float vertical) noexcept;

        /**
         * @brief Returns the bounded delta for this frame and retains the remainder.
         * @param deltaSeconds Current frame duration in seconds.
         * @return Wheel input admitted for this frame, or an empty delta for an invalid duration.
         */
        [[nodiscard]] EditorScrollDelta Consume(float deltaSeconds) noexcept;

        /** @brief Drops all queued wheel input. */
        void Reset() noexcept;

    private:
        float pendingHorizontal_{};
        float pendingVertical_{};
    };
}  // namespace Horo::Editor
