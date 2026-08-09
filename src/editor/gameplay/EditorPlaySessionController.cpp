#include "editor/gameplay/EditorPlaySessionController.h"

#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <ranges>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] Error InvalidTransition(const char *message) {
            return MakeError(Gameplay::GameplayErrors::InvalidBehaviorComponent, message);
        }
    }  // namespace

    /** @copydoc EditorPlaySessionController::Start */
    Result<void> EditorPlaySessionController::Start(const SceneDocumentSnapshot &authoring, const Gameplay::BehaviorRegistry &registry,
                                                    std::unique_ptr<Runtime::RuntimeScene> preparedScene) {
        if (state_ != EditorPlaySessionState::Idle && state_ != EditorPlaySessionState::Failed)
            return Result<void>::Failure(InvalidTransition("A play session is already active."));
        Stop();
        state_ = EditorPlaySessionState::Starting;
        lastError_.reset();
        authoringRevision_ = authoring.revision;

        if (std::ranges::none_of(authoring.objects, [](const SceneObjectSnapshot &object) {
            return object.components.camera.has_value();
        })) {
            Error error = InvalidTransition("Play Mode requires an authored camera component.");
            Fail(error);
            return Result<void>::Failure(std::move(error));
        }

        if (preparedScene) {
            if (preparedScene->View().DefinitionRevision().value != authoring.state.value) {
                Error error = InvalidTransition("The prepared authoring preview is stale; wait for scene synchronization and retry.");
                Fail(error);
                return Result<void>::Failure(std::move(error));
            }
            scene_ = std::move(preparedScene);
        } else {
            Result<Runtime::RuntimeSceneDefinition> converted = ConvertSceneDocumentToRuntime(authoring, Runtime::SceneDefinitionId{2});
            if (converted.HasError()) {
                Error error = converted.ErrorValue();
                Fail(error);
                return Result<void>::Failure(std::move(error));
            }
            Result<std::unique_ptr<Runtime::RuntimeScene>> created =
                Runtime::RuntimeScene::Create(converted.Value(), Runtime::SceneRuntimeId{nextRuntimeId_++});
            if (created.HasError()) {
                Error error = created.ErrorValue();
                Fail(error);
                return Result<void>::Failure(std::move(error));
            }
            scene_ = std::move(created).Value();
        }

        Result<std::unique_ptr<Gameplay::BehaviorRuntime>> runtime = Gameplay::BehaviorRuntime::Create(*scene_, registry);
        if (runtime.HasError()) {
            Error error = runtime.ErrorValue();
            Fail(error);
            return Result<void>::Failure(std::move(error));
        }
        behaviors_ = std::move(runtime).Value();
        state_ = EditorPlaySessionState::Playing;
        return Result<void>::Success();
    }

    /** @copydoc EditorPlaySessionController::Pause */
    Result<void> EditorPlaySessionController::Pause() {
        if (state_ != EditorPlaySessionState::Playing)
            return Result<void>::Failure(InvalidTransition("Only a playing session can be paused."));
        state_ = EditorPlaySessionState::Paused;
        return Result<void>::Success();
    }

    /** @copydoc EditorPlaySessionController::Resume */
    Result<void> EditorPlaySessionController::Resume() {
        if (state_ != EditorPlaySessionState::Paused)
            return Result<void>::Failure(InvalidTransition("Only a paused session can be resumed."));
        stepPending_ = false;
        state_ = EditorPlaySessionState::Playing;
        return Result<void>::Success();
    }

    /** @copydoc EditorPlaySessionController::Step */
    Result<void> EditorPlaySessionController::Step() {
        if (state_ != EditorPlaySessionState::Paused)
            return Result<void>::Failure(InvalidTransition("Step is available only while paused."));
        stepPending_ = true;
        return Result<void>::Success();
    }

    /** @copydoc EditorPlaySessionController::Stop */
    void EditorPlaySessionController::Stop() noexcept {
        if (state_ != EditorPlaySessionState::Idle)
            state_ = EditorPlaySessionState::Stopping;
        if (behaviors_)
            behaviors_->Shutdown();
        behaviors_.reset();
        scene_.reset();
        stepPending_ = false;
        authoringRevision_ = {};
        state_ = EditorPlaySessionState::Idle;
    }

    /** @copydoc EditorPlaySessionController::FixedUpdate */
    Result<void> EditorPlaySessionController::FixedUpdate(std::span<const Gameplay::GameplayInputAction> input,
                                                          const Gameplay::FixedDeltaTime delta) {
        const bool shouldTick = state_ == EditorPlaySessionState::Playing || (state_ == EditorPlaySessionState::Paused && stepPending_);
        if (!shouldTick)
            return Result<void>::Success();
        stepPending_ = false;
        Result<void> updated = behaviors_->FixedUpdate(input, delta);
        if (updated.HasError()) {
            Error error = updated.ErrorValue();
            Fail(error);
            return Result<void>::Failure(std::move(error));
        }
        return Result<void>::Success();
    }

    /** @copydoc EditorPlaySessionController::PresentationUpdate */
    void EditorPlaySessionController::PresentationUpdate(const Gameplay::FrameDeltaTime delta) {
        if (behaviors_ && (state_ == EditorPlaySessionState::Playing || state_ == EditorPlaySessionState::Paused))
            behaviors_->PresentationUpdate(delta);
    }

    /** @copydoc EditorPlaySessionController::ReloadBehaviors */
    Result<void> EditorPlaySessionController::ReloadBehaviors(const Gameplay::BehaviorRegistry &candidate,
                                                              const Gameplay::BehaviorRegistry &rollback) {
        if (!scene_ || (state_ != EditorPlaySessionState::Playing && state_ != EditorPlaySessionState::Paused))
            return Result<void>::Failure(InvalidTransition("Behavior reload requires an active play session."));

        behaviors_->Shutdown();
        behaviors_.reset();
        Result<std::unique_ptr<Gameplay::BehaviorRuntime>> replacement = Gameplay::BehaviorRuntime::Create(*scene_, candidate);
        if (replacement.HasValue()) {
            behaviors_ = std::move(replacement).Value();
            return Result<void>::Success();
        }

        Error candidateError = replacement.ErrorValue();
        Result<std::unique_ptr<Gameplay::BehaviorRuntime>> restored = Gameplay::BehaviorRuntime::Create(*scene_, rollback);
        if (restored.HasValue()) {
            behaviors_ = std::move(restored).Value();
            return Result<void>::Failure(std::move(candidateError));
        }
        Fail(restored.ErrorValue());
        return Result<void>::Failure(std::move(candidateError));
    }

    EditorPlaySessionState EditorPlaySessionController::State() const noexcept {
        return state_;
    }

    bool EditorPlaySessionController::IsActive() const noexcept {
        return state_ == EditorPlaySessionState::Starting || state_ == EditorPlaySessionState::Playing ||
               state_ == EditorPlaySessionState::Paused || state_ == EditorPlaySessionState::Stopping;
    }

    Runtime::RuntimeScene *EditorPlaySessionController::Scene() noexcept {
        return scene_.get();
    }

    const Runtime::RuntimeScene *EditorPlaySessionController::Scene() const noexcept {
        return scene_.get();
    }

    const std::optional<Error> &EditorPlaySessionController::LastError() const noexcept {
        return lastError_;
    }

    DocumentRevision EditorPlaySessionController::AuthoringRevision() const noexcept {
        return authoringRevision_;
    }

    void EditorPlaySessionController::Fail(Error error) noexcept {
        if (behaviors_)
            behaviors_->Shutdown();
        behaviors_.reset();
        scene_.reset();
        stepPending_ = false;
        lastError_ = std::move(error);
        state_ = EditorPlaySessionState::Failed;
    }
}  // namespace Horo::Editor
