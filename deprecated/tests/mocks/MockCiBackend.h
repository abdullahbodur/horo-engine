/** @file MockCiBackend.h
 *  @brief Test mock for CI remote build dispatch — in-process, no network.
 *
 *  Injected into EditorBuildPipelineModal during UI automation scenarios
 *  so the build flow can be exercised end-to-end without a real CI service.
 *  Each call to Poll() advances a simple state machine:
 *  Pending → Building → Success (with an artifact path).
 */
#pragma once

#ifdef HORO_STANDALONE_UI_AUTOMATION

#include <string>
#include <vector>

#include "core/pipeline/ReleasePipeline.h"

namespace Horo::Test {

/** @brief Deterministic mock CI backend that simulates remote build dispatch.
 *
 *  The mock owns a local copy of the build jobs and advances them through
 *  a fixed sequence on each Poll() call.  The scenario drives the lifecycle:
 *  1. Inject via EditorBuildPipelineModal::SetMockCiBackend()
 *  2. Open the modal and select CI build mode
 *  3. Start the build → mock transitions jobs Pending→Building
 *  4. Call AdvanceStatus() → mock transitions Building→Success
 *  5. Verify artifact-ready UI state
 */
class MockCiBackend {
public:
    /** @brief Accepts a set of pending build jobs to simulate. */
    void DispatchJobs(std::vector<Horo::Build::BuildJob> jobs) {
        m_jobs = std::move(jobs);
        m_advanceStep = 0;
    }

    /** @brief Advances the mock state machine one step.
     *
     *  Step 0 → 1: pending → building
     *  Step 1 → 2: building → success with artifact output
     */
    void AdvanceStatus() {
        if (m_jobs.empty())
            return;

        ++m_advanceStep;

        for (auto &job : m_jobs) {
            if (m_advanceStep == 1 &&
                job.status == Horo::Build::BuildJobStatus::Pending) {
                job.status = Horo::Build::BuildJobStatus::Building;
                job.log = "CI: dispatching build...\n";
            } else if (m_advanceStep == 2 &&
                       job.status == Horo::Build::BuildJobStatus::Building) {
                job.status = Horo::Build::BuildJobStatus::Success;
                job.exitCode = 0;
                job.outputPath = "/ci/artifacts/" + job.log.substr(0, 8) + ".zip";
                job.log += "CI: build complete.\nCI: artifacts uploaded.\n";
                job.timestamp = Horo::Build::CurrentTimestamp();
            }
        }
    }

    /** @brief Returns the simulated build jobs for status-driven verification. */
    const std::vector<Horo::Build::BuildJob> &Jobs() const { return m_jobs; }

    /** @brief Returns true when every job has reached a terminal state. */
    bool AllJobsTerminal() const {
        for (const auto &job : m_jobs) {
            using enum Horo::Build::BuildJobStatus;
            if (job.status != Success &&
                job.status != Failed &&
                job.status != Cancelled)
                return false;
        }
        return !m_jobs.empty();
    }

    /** @brief Resets the mock to its initial empty state. */
    void Reset() {
        m_jobs.clear();
        m_advanceStep = 0;
    }

private:
    std::vector<Horo::Build::BuildJob> m_jobs;
    int m_advanceStep = 0;
};

} // namespace Horo::Test

#endif // HORO_STANDALONE_UI_AUTOMATION
