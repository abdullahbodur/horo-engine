# cmake/GenerateBuildInfo.cmake
#
# Runs scripts/parse_changelog.py at configure time to produce
# ${HORO_GENERATED_DIR}/GeneratedBuildInfo.h from CHANGELOG.md.
#
# The generated file is re-created whenever CHANGELOG.md changes because
# CMake tracks the DEPENDS list on the custom command.
#
# Variables set by this module (available to parent scope after include):
#   HORO_GENERATED_DIR   — directory containing GeneratedBuildInfo.h

set(HORO_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated" CACHE INTERNAL "")
set(_CHANGELOG "${PROJECT_SOURCE_DIR}/CHANGELOG.md")
set(_OUTPUT    "${HORO_GENERATED_DIR}/GeneratedBuildInfo.h")
set(_SCRIPT    "${PROJECT_SOURCE_DIR}/scripts/parse_changelog.py")

# ── Locate Python 3 ─────────────────────────────────────────────────────────
find_package(Python3 QUIET COMPONENTS Interpreter)

if(NOT Python3_FOUND)
    message(WARNING
        "[GenerateBuildInfo] Python 3 not found — "
        "GeneratedBuildInfo.h will contain fallback (no release notes)."
    )
endif()

# ── Configure-time generation ────────────────────────────────────────────────
# Run once immediately so the header exists before any target tries to include it.
if(Python3_FOUND)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_SCRIPT}" "${_CHANGELOG}" "${_OUTPUT}"
        RESULT_VARIABLE _parse_result
        ERROR_VARIABLE  _parse_err
    )
    if(NOT _parse_result EQUAL 0)
        message(WARNING "[GenerateBuildInfo] parse_changelog.py failed: ${_parse_err}")
    endif()
else()
    # Write a minimal fallback header so the build does not break.
    file(MAKE_DIRECTORY "${HORO_GENERATED_DIR}")
    file(WRITE "${_OUTPUT}"
"// This file is AUTO-GENERATED — Python 3 was not found at configure time.
// Rebuild after installing Python 3 to populate release notes.
#pragma once

namespace Horo::Generated {
struct WhatsNewEntry {
    const char* tag;
    const char* title;
    const char* body;
};
inline constexpr int kWhatsNewCount = 2;
inline constexpr WhatsNewEntry kWhatsNewEntries[2] = {
    {\"Release Notes\", \"No releases yet\", \"\"},
    {\"Release Notes\", \"No releases yet\", \"\"},
};
} // namespace Horo::Generated
")
endif()

# ── Build-time regeneration on CHANGELOG.md change ──────────────────────────
# A custom target that reruns the script when CHANGELOG.md is newer than the header.
# This fires during incremental builds without a full reconfigure.
if(Python3_FOUND)
    add_custom_command(
        OUTPUT  "${_OUTPUT}"
        COMMAND "${Python3_EXECUTABLE}" "${_SCRIPT}" "${_CHANGELOG}" "${_OUTPUT}"
        DEPENDS "${_CHANGELOG}" "${_SCRIPT}"
        COMMENT "Regenerating GeneratedBuildInfo.h from CHANGELOG.md"
        VERBATIM
    )
    add_custom_target(HoroBuildInfo ALL DEPENDS "${_OUTPUT}")
endif()
