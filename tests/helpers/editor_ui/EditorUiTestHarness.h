#pragma once

#include "EditorUiTestSurface.h"
#include "Horo/Runtime/Input.h"

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiContext;
struct ImGuiTestContext;
struct ImGuiTestEngine;

namespace Horo::Tests
{
    /** @brief Deterministic frame limits for one headless editor UI scenario. */
    struct EditorUiScenarioBudget
    {
        std::size_t maximumFrames{600};

        /** @brief Creates the default deterministic scenario budget. */
        [[nodiscard]] static constexpr EditorUiScenarioBudget Default() noexcept
        {
            return {};
        }

        /** @brief Creates an explicitly justified extended budget, capped at 1800 frames. */
        [[nodiscard]] static EditorUiScenarioBudget Extended(std::size_t maximumFrames);
    };

    /** @brief Terminal state of one named operation in a UI scenario pipeline. */
    enum class UiScenarioStepStatus
    {
        Passed,
        Failed,
        Skipped,
    };

    /** @brief Role of one named child operation in a full editor scenario. */
    enum class UiScenarioStepKind
    {
        Setup,
        Test,
    };

    /** @brief Deterministic result of one named UI scenario operation. */
    struct UiScenarioStepResult
    {
        std::string name;
        UiScenarioStepKind kind{UiScenarioStepKind::Test};
        UiScenarioStepStatus status{UiScenarioStepStatus::Skipped};
        std::size_t firstFrame{0};
        std::size_t lastFrame{0};
        std::string diagnostic;
    };

    /** @brief Result transferred from Test Engine's coroutine to the owning Catch2 thread. */
    struct EditorUiScenarioResult
    {
        int testsRun{0};
        int testsSucceeded{0};
        std::size_t simulatedFrames{0};
        bool frameBudgetExceeded{false};
        bool cancelled{false};
        std::exception_ptr exception;
        std::string testEngineLog;
        std::vector<UiScenarioStepResult> steps;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return !frameBudgetExceeded && !cancelled && exception == nullptr && testsRun == 1 && testsSucceeded == 1;
        }
    };

    /**
     * @brief Executes named UI operations sequentially inside one Test Engine TestFunc.
     *
     * A failed operation prevents later operations from running. Results remain
     * operation-owned until the Test Engine coroutine has joined and Catch2 may
     * safely inspect them on its owning thread.
     */
    class UiScenarioPipe
    {
    public:
        using StepFunction = std::function<void(ImGuiTestContext&)>;

        UiScenarioPipe(ImGuiTestContext& context, EditorUiScenarioResult& result) noexcept;

        /** @brief Sets the base Test Engine path reapplied to every child test. */
        UiScenarioPipe& SetRef(std::string_view reference);

        /** @brief Executes one named operation, or records it as skipped after an earlier failure. */
        UiScenarioPipe& Step(std::string_view name, StepFunction operation);

        /** @brief Executes reusable prerequisite UI navigation as a named fast child test. */
        UiScenarioPipe& Setup(std::string_view name, StepFunction operation);

    private:
        UiScenarioPipe& RunChild(UiScenarioStepKind kind, std::string_view name, StepFunction operation);

        ImGuiTestContext& context_;
        EditorUiScenarioResult& result_;
        std::string reference_;
        std::size_t stepIndex_{0};
    };

    /** @brief Returns a stable diagnostic label for a UI scenario step status. */
    [[nodiscard]] const char* ToString(UiScenarioStepStatus status) noexcept;

    /** @brief Converts the current ImGui IO state into the runtime input snapshot used by editor code. */
    class TestInputBridge
    {
    public:
        explicit TestInputBridge(Input::InputRouter* router = nullptr) noexcept;

        void BeginEditorFrame();

    private:
        Input::RawInputCollector collector_;
        Input::InputRouter* router_{nullptr};
        Input::FrameNumber frame_{1};
    };

    /**
     * @brief Owns one instrumented ImGui/Test Engine context for one Catch2 scenario.
     *
     * The harness is intentionally process-local and non-concurrent. The official
     * std::thread coroutine implementation hands execution between the UI and test
     * threads; results are inspected only after Stop() joins the child.
     */
    class EditorUiTestHarness
    {
    public:
        using GuiFunction = std::function<void(ImGuiTestContext*)>;
        using TestFunction = std::function<void(ImGuiTestContext*)>;
        using ScenarioFunction = std::function<void(UiScenarioPipe&)>;

        EditorUiTestHarness();
        explicit EditorUiTestHarness(std::unique_ptr<IEditorUiTestSurface> surface);
        ~EditorUiTestHarness();
        EditorUiTestHarness(const EditorUiTestHarness&) = delete;
        EditorUiTestHarness& operator=(const EditorUiTestHarness&) = delete;

        [[nodiscard]] EditorUiScenarioResult Run(std::string category, std::string name, GuiFunction gui,
                                                 TestFunction test,
                                                 EditorUiScenarioBudget budget = EditorUiScenarioBudget::Default(),
                                                 Input::InputRouter* router = nullptr);

        /** @brief Runs a named, sequential scenario pipeline inside Test Engine's TestFunc. */
        [[nodiscard]] EditorUiScenarioResult RunScenario(std::string category, std::string name, GuiFunction gui,
                                                         ScenarioFunction scenario,
                                                         EditorUiScenarioBudget budget =
                                                             EditorUiScenarioBudget::Default(),
                                                         Input::InputRouter* router = nullptr);

        /** @brief Requests deterministic cancellation at the next main-thread frame boundary. */
        void RequestCancel() noexcept;

        /** @brief Returns the initialized renderer surface for full-editor host composition. */
        [[nodiscard]] IEditorUiTestSurface& Surface() noexcept;

    private:
        using InternalTestFunction = std::function<void(ImGuiTestContext*, EditorUiScenarioResult&)>;

        [[nodiscard]] EditorUiScenarioResult RunInternal(std::string category, std::string name, GuiFunction gui,
                                                         InternalTestFunction test, EditorUiScenarioBudget budget,
                                                         Input::InputRouter* router);

        std::unique_ptr<IEditorUiTestSurface> surface_;
        ImGuiContext* context_{nullptr};
        ImGuiTestEngine* engine_{nullptr};
        bool started_{false};
        bool ranScenario_{false};
        bool ownsProcessAdmission_{false};
        std::atomic_bool cancelRequested_{false};
    };
} // namespace Horo::Tests
