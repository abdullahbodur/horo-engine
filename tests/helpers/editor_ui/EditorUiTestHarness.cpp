#include "EditorUiTestHarness.h"

#include <imgui.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace Horo::Tests
{
namespace
{
std::atomic_bool gHarnessActive{false};

[[nodiscard]] Input::Key MapKey(const ImGuiKey key) noexcept
{
    switch (key)
    {
    case ImGuiKey_A:
        return Input::Key::A;
    case ImGuiKey_D:
        return Input::Key::D;
    case ImGuiKey_E:
        return Input::Key::E;
    case ImGuiKey_F:
        return Input::Key::F;
    case ImGuiKey_Q:
        return Input::Key::Q;
    case ImGuiKey_R:
        return Input::Key::R;
    case ImGuiKey_S:
        return Input::Key::S;
    case ImGuiKey_W:
        return Input::Key::W;
    case ImGuiKey_Escape:
        return Input::Key::Escape;
    case ImGuiKey_Enter:
        return Input::Key::Enter;
    case ImGuiKey_Delete:
        return Input::Key::Delete;
    case ImGuiKey_F2:
        return Input::Key::F2;
    default:
        return Input::Key::Unknown;
    }
}

constexpr ImGuiKey kBridgedKeys[]{ImGuiKey_A,      ImGuiKey_D,     ImGuiKey_E,      ImGuiKey_F,
                                  ImGuiKey_Q,      ImGuiKey_R,     ImGuiKey_S,      ImGuiKey_W,
                                  ImGuiKey_Escape, ImGuiKey_Enter, ImGuiKey_Delete, ImGuiKey_F2};
} // namespace

const char *ToString(const UiScenarioStepStatus status) noexcept
{
    switch (status)
    {
    case UiScenarioStepStatus::Passed:
        return "passed";
    case UiScenarioStepStatus::Failed:
        return "failed";
    case UiScenarioStepStatus::Skipped:
        return "skipped";
    }
    return "unknown";
}

UiScenarioPipe::UiScenarioPipe(ImGuiTestContext &context, EditorUiScenarioResult &result) noexcept
    : context_(context), result_(result)
{
}

UiScenarioPipe &UiScenarioPipe::SetRef(const std::string_view reference)
{
    reference_.assign(reference);
    return *this;
}

UiScenarioPipe &UiScenarioPipe::Step(const std::string_view name, StepFunction operation)
{
    return RunChild(UiScenarioStepKind::Test, name, std::move(operation));
}

UiScenarioPipe &UiScenarioPipe::Setup(const std::string_view name, StepFunction operation)
{
    return RunChild(UiScenarioStepKind::Setup, name, std::move(operation));
}

UiScenarioPipe &UiScenarioPipe::RunChild(const UiScenarioStepKind kind, const std::string_view name,
                                         StepFunction operation)
{
    UiScenarioStepResult step;
    step.name.assign(name);
    step.kind = kind;
    step.firstFrame = static_cast<std::size_t>(context_.FrameCount);
    step.lastFrame = step.firstFrame;

    if (context_.IsError() || result_.exception != nullptr || result_.cancelled)
    {
        step.status = UiScenarioStepStatus::Skipped;
        step.diagnostic = "Skipped after an earlier scenario failure.";
        result_.steps.push_back(std::move(step));
        return *this;
    }

    const std::string childName =
        std::string{context_.Test->Name} + "::step_" + std::to_string(stepIndex_++) + "::" + step.name;
    ImGuiTest *child = IM_REGISTER_TEST(context_.Engine, "horo_scenario_step", childName.c_str());
    child->SetOwnedName(childName.c_str());
    child->TestFunc = [operation = std::move(operation), reference = reference_,
                       &result = result_](ImGuiTestContext *childContext) {
        try
        {
            if (!reference.empty())
                childContext->SetRef(reference.c_str());
            operation(*childContext);
        }
        catch (...)
        {
            result.exception = std::current_exception();
            childContext->Abort = true;
        }
    };

    context_.LogInfo(kind == UiScenarioStepKind::Setup ? "[setup] %s" : "[test] %s", step.name.c_str());
    const ImGuiTestRunSpeed previousSpeed = context_.EngineIO->ConfigRunSpeed;
    if (kind == UiScenarioStepKind::Setup)
        context_.EngineIO->ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    const ImGuiTestStatus childStatus = context_.RunChildTest(childName.c_str(), ImGuiTestRunFlags_ShareTestContext);
    context_.EngineIO->ConfigRunSpeed = previousSpeed;

    step.lastFrame = static_cast<std::size_t>(context_.FrameCount);
    if (result_.exception != nullptr)
    {
        step.status = UiScenarioStepStatus::Failed;
        step.diagnostic = "The step raised an unexpected C++ exception.";
    }
    else if (childStatus != ImGuiTestStatus_Success || context_.IsError())
    {
        step.status = UiScenarioStepStatus::Failed;
        step.diagnostic = "Dear ImGui Test Engine reported a failed operation or check.";
    }
    else
    {
        step.status = UiScenarioStepStatus::Passed;
    }
    result_.steps.push_back(std::move(step));
    return *this;
}

EditorUiScenarioBudget EditorUiScenarioBudget::Extended(const std::size_t maximumFrames)
{
    if (maximumFrames <= 600 || maximumFrames > 1800)
        throw std::invalid_argument("An extended UI scenario budget must be in the range 601..1800 frames.");
    return {.maximumFrames = maximumFrames};
}

TestInputBridge::TestInputBridge(Input::InputRouter *router) noexcept : router_(router)
{
}

void TestInputBridge::BeginEditorFrame()
{
    if (router_ == nullptr)
        return;

    const ImGuiIO &io = ImGui::GetIO();
    collector_.BeginFrame(frame_++);
    collector_.SetPointerPosition(io.MousePos.x, io.MousePos.y);
    collector_.SetPointerButton(Input::PointerButton::Primary, io.MouseDown[ImGuiMouseButton_Left]);
    collector_.SetPointerButton(Input::PointerButton::Secondary, io.MouseDown[ImGuiMouseButton_Right]);
    collector_.SetPointerButton(Input::PointerButton::Middle, io.MouseDown[ImGuiMouseButton_Middle]);
    collector_.AddPointerWheel(io.MouseWheelH, io.MouseWheel);
    for (const ImGuiKey key : kBridgedKeys)
        collector_.SetKey(MapKey(key), ImGui::IsKeyDown(key));
    collector_.SetModifiers(Input::ModifierState{
        .control = io.KeyCtrl,
        .shift = io.KeyShift,
        .alt = io.KeyAlt,
        .command = io.KeySuper,
    });
    router_->BeginFrame(collector_.Commit());
}

EditorUiTestHarness::EditorUiTestHarness() : EditorUiTestHarness(CreateEditorUiTestSurfaceFromEnvironment())
{
}

EditorUiTestHarness::EditorUiTestHarness(std::unique_ptr<IEditorUiTestSurface> surface) : surface_(std::move(surface))
{
    if (surface_ == nullptr)
        throw std::invalid_argument("EditorUiTestHarness requires a presentation surface.");

    bool expected = false;
    if (!gHarnessActive.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        throw std::logic_error("Only one EditorUiTestHarness may own an ImGui context in a process.");
    ownsProcessAdmission_ = true;

    try
    {
        IMGUI_CHECKVERSION();
        context_ = ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.Fonts->AddFontDefault();
        static_cast<void>(io.Fonts->Build());

        surface_->Initialize(*context_);

        engine_ = ImGuiTestEngine_CreateContext();
        ImGuiTestEngineIO &testIo = ImGuiTestEngine_GetIO(engine_);
        testIo.ConfigRunSpeed = surface_->IsInteractive() ? ImGuiTestRunSpeed_Normal : ImGuiTestRunSpeed_Fast;
        testIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
        testIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
        testIo.ConfigSavedSettings = false;
        ImGuiTestEngine_Start(engine_, ImGui::GetCurrentContext());
        started_ = true;
    }
    catch (...)
    {
        if (engine_ != nullptr)
        {
            ImGuiTestEngine_DestroyContext(engine_);
            engine_ = nullptr;
        }
        surface_->Shutdown();
        if (context_ != nullptr)
        {
            ImGui::DestroyContext(context_);
            context_ = nullptr;
        }
        ownsProcessAdmission_ = false;
        gHarnessActive.store(false, std::memory_order_release);
        throw;
    }
}

EditorUiTestHarness::~EditorUiTestHarness()
{
    if (context_ != nullptr)
        ImGui::SetCurrentContext(context_);
    if (engine_ != nullptr && started_)
        ImGuiTestEngine_Stop(engine_);
    if (surface_ != nullptr)
        surface_->Shutdown();
    if (context_ != nullptr)
        ImGui::DestroyContext(context_);
    if (engine_ != nullptr)
        ImGuiTestEngine_DestroyContext(engine_);
    if (ownsProcessAdmission_)
        gHarnessActive.store(false, std::memory_order_release);
}

EditorUiScenarioResult EditorUiTestHarness::Run(std::string category, std::string name, GuiFunction gui,
                                                TestFunction test, const EditorUiScenarioBudget budget,
                                                Input::InputRouter *router)
{
    return RunInternal(
        std::move(category), std::move(name), std::move(gui),
        [test = std::move(test)](ImGuiTestContext *context, EditorUiScenarioResult &) { test(context); }, budget,
        router);
}

EditorUiScenarioResult EditorUiTestHarness::RunScenario(std::string category, std::string name, GuiFunction gui,
                                                        ScenarioFunction scenario, const EditorUiScenarioBudget budget,
                                                        Input::InputRouter *router)
{
    return RunInternal(
        std::move(category), std::move(name), std::move(gui),
        [scenario = std::move(scenario)](ImGuiTestContext *context, EditorUiScenarioResult &result) {
            UiScenarioPipe pipe{*context, result};
            scenario(pipe);
        },
        budget, router);
}

EditorUiScenarioResult EditorUiTestHarness::RunInternal(std::string category, std::string name, GuiFunction gui,
                                                        InternalTestFunction test, const EditorUiScenarioBudget budget,
                                                        Input::InputRouter *router)
{
    if (ranScenario_)
        throw std::logic_error("EditorUiTestHarness accepts exactly one scenario per context.");
    ranScenario_ = true;
    if (budget.maximumFrames == 0 || budget.maximumFrames > 1800)
        throw std::invalid_argument("UI scenario frame budget must be in the range 1..1800.");

    EditorUiScenarioResult result;
    std::atomic_bool coroutineAbortRequested{false};
    TestInputBridge bridge{router};
    ImGuiTest *registered = ImGuiTestEngine_RegisterTest(engine_, category.c_str(), name.c_str(), __FILE__, __LINE__);
    registered->SetOwnedName(name.c_str());
    registered->GuiFunc = [gui = std::move(gui), &bridge, &result,
                           &coroutineAbortRequested](ImGuiTestContext *context) {
        try
        {
            bridge.BeginEditorFrame();
            gui(context);
        }
        catch (...)
        {
            result.exception = std::current_exception();
            coroutineAbortRequested.store(true, std::memory_order_release);
        }
    };
    registered->TestFunc = [test = std::move(test), &result, &coroutineAbortRequested](ImGuiTestContext *context) {
        try
        {
            test(context, result);
        }
        catch (...)
        {
            result.exception = std::current_exception();
            coroutineAbortRequested.store(true, std::memory_order_release);
        }
    };

    ImGuiTestEngine_QueueTest(engine_, registered);
    while (!ImGuiTestEngine_IsTestQueueEmpty(engine_) && result.simulatedFrames < budget.maximumFrames)
    {
        try
        {
            if (!surface_->BeginFrame())
            {
                result.cancelled = true;
                ImGuiTestEngine_AbortCurrentTest(engine_);
                break;
            }
            ImGui::NewFrame();
            ImGui::Render();
            surface_->Present();
            ImGuiTestEngine_PostSwap(engine_);
        }
        catch (...)
        {
            result.exception = std::current_exception();
            ImGuiTestEngine_AbortCurrentTest(engine_);
            break;
        }
        ++result.simulatedFrames;
        if (coroutineAbortRequested.load(std::memory_order_acquire))
        {
            ImGuiTestEngine_AbortCurrentTest(engine_);
            break;
        }
        if (cancelRequested_.load(std::memory_order_acquire))
        {
            result.cancelled = true;
            ImGuiTestEngine_AbortCurrentTest(engine_);
            break;
        }
    }
    if (!result.cancelled && !coroutineAbortRequested.load(std::memory_order_acquire) &&
        !ImGuiTestEngine_IsTestQueueEmpty(engine_))
    {
        result.frameBudgetExceeded = true;
        ImGuiTestEngine_AbortCurrentTest(engine_);
    }

    ImGuiTestEngine_Stop(engine_);
    started_ = false;
    ImGuiTestEngine_GetResult(engine_, result.testsRun, result.testsSucceeded);
    ImGuiTextBuffer log;
    registered->Output.Log.ExtractLinesForVerboseLevels(ImGuiTestVerboseLevel_Error, ImGuiTestVerboseLevel_Trace, &log);
    result.testEngineLog = log.c_str();
    return result;
}

void EditorUiTestHarness::RequestCancel() noexcept
{
    cancelRequested_.store(true, std::memory_order_release);
}
} // namespace Horo::Tests
