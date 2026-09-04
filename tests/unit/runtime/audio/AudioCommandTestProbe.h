#pragma once
#include <cstddef>

namespace Horo::Audio::Test {
    // Per-thread probes are defined with the command test executable's global new/delete replacements.
    extern thread_local std::size_t allocationCount;
    extern thread_local std::size_t deallocationCount;
    extern thread_local std::size_t failCountdown;
}  // namespace Horo::Audio::Test
