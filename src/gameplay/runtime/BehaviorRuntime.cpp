#include "Horo/Gameplay/BehaviorRuntime.h"

#include "Horo/Foundation/Logging/Logger.h"
#include "Horo/Gameplay/GameplayErrors.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace Horo::Gameplay {
    namespace {
        [[nodiscard]] GameplayEntityRef ToGameplay(const Runtime::EntityRef entity) noexcept {
            return {entity.runtime.value, entity.entity.index, entity.entity.generation};
        }

        [[nodiscard]] Runtime::EntityRef ToRuntime(const GameplayEntityRef entity) noexcept {
            return {Runtime::SceneRuntimeId{entity.scene}, Runtime::EntityId{entity.index, entity.generation}};
        }

        struct EventQueue {
            explicit EventQueue(const std::size_t maximum) : maximum(maximum) {}

            [[nodiscard]] Result<void> Publish(GameplayEvent event) {
                if (!event.type.IsValid() || event.schemaVersion == 0 || event.fields.size() > MaximumBehaviorFields)
                    return Result<void>::Failure(MakeError(GameplayErrors::InvalidEvent));
                if (next.size() >= maximum)
                    return Result<void>::Failure(MakeError(GameplayErrors::EventQueueFull));
                next.push_back(std::move(event));
                return Result<void>::Success();
            }

            void BeginTick() {
                current = std::move(next);
                next.clear();
            }

            std::size_t maximum;
            std::vector<GameplayEvent> current;
            std::vector<GameplayEvent> next;
        };
    }  // namespace

    struct BehaviorRuntime::Impl {
        struct Instance {
            Runtime::EntityRef entity;
            BehaviorComponent component;
            const BehaviorRegistration *registration{};
            std::unique_ptr<IBehaviorInstance> implementation;
            bool created{};
            bool enabledCallbackActive{};
            bool started{};
        };

        struct ContextBackend final : Detail::IBehaviorContextBackend {
            ContextBackend(Impl &runtime, Instance &instance, const std::span<const GameplayInputAction> input,
                           Runtime::SceneCommandBuffer &commands, const bool allowSimulationMutation = true) noexcept
                : runtime(runtime), instance(instance), input(input), commands(commands), allowSimulationMutation(allowSimulationMutation) {
            }

            Impl &runtime;
            Instance &instance;
            std::span<const GameplayInputAction> input;
            Runtime::SceneCommandBuffer &commands;
            bool allowSimulationMutation{true};

            [[nodiscard]] GameplayEntityRef Entity() const noexcept override {
                return ToGameplay(instance.entity);
            }

            [[nodiscard]] BehaviorInstanceId InstanceId() const noexcept override {
                return instance.component.instanceId;
            }

            [[nodiscard]] std::span<const BehaviorField> Fields() const noexcept override {
                return instance.component.fields;
            }

            [[nodiscard]] std::span<const GameplayInputAction> InputActions() const noexcept override {
                return input;
            }

            [[nodiscard]] Result<Math::Transform> LocalTransform() const override {
                const Result<Runtime::RuntimeEntityView> view = runtime.scene.View().Get(instance.entity);
                if (view.HasError())
                    return Result<Math::Transform>::Failure(view.ErrorValue());
                return Result<Math::Transform>::Success(*view.Value().localTransform);
            }

            [[nodiscard]] Result<void> SetLocalTransform(const Math::Transform &transform) override {
                if (!allowSimulationMutation || transform.TryToMatrix().HasError())
                    return Result<void>::Failure(
                        MakeError(GameplayErrors::InvalidBehaviorComponent, "This lifecycle phase cannot mutate simulation transforms."));
                commands.SetLocalTransform(instance.entity, transform);
                return Result<void>::Success();
            }

            [[nodiscard]] Result<void> Publish(GameplayEvent event) override {
                if (event.target && (!event.target->IsValid() || event.target->scene != instance.entity.runtime.value))
                    return Result<void>::Failure(MakeError(GameplayErrors::InvalidEvent));
                return runtime.events.Publish(std::move(event));
            }
        };

        Impl(Runtime::RuntimeScene &scene, const BehaviorRegistry &registry, const BehaviorRuntimeLimits limits)
            : scene(scene), registry(registry), limits(limits), events(limits.maximumQueuedEvents) {}

        [[nodiscard]] Result<void> BuildInstances() {
            if (!registry.IsFrozen())
                return Result<void>::Failure(
                    MakeError(GameplayErrors::RegistryFrozen, "Behavior registry must be frozen before scene activation."));

            const Runtime::RuntimeSceneView sceneView = scene.View();
            for (std::size_t slot = 0; slot < sceneView.SlotCount(); ++slot) {
                const std::optional<Runtime::RuntimeEntityView> entity = sceneView.EntityAt(slot);
                if (!entity)
                    continue;
                std::unordered_map<std::string_view, std::size_t> multiplicity;
                for (const BehaviorComponent &component : entity->components->behaviors) {
                    if (instances.size() >= limits.maximumInstances)
                        return Result<void>::Failure(
                            MakeError(GameplayErrors::InvalidBehaviorComponent, "Scene behavior instance budget was exceeded."));
                    const BehaviorRegistration *registration = registry.Find(component.typeId);
                    if (registration == nullptr)
                        return Result<void>::Failure(
                            MakeError(GameplayErrors::BehaviorNotRegistered,
                                      "Scene references unavailable behavior '" + component.typeId.Value() + "'."));
                    if (const std::size_t count = ++multiplicity[component.typeId.Value()];
                        count > 1 && !registration->descriptor.allowMultiple)
                        return Result<void>::Failure(MakeError(GameplayErrors::BehaviorMultiplicityViolation));
                    std::unique_ptr<IBehaviorInstance> implementation = registration->factory.create();
                    if (implementation == nullptr)
                        return Result<void>::Failure(
                            MakeError(GameplayErrors::InvalidBehaviorComponent, "Behavior factory returned no instance."));
                    instances.emplace_back(entity->entity, component, registration, std::move(implementation));
                }
            }

            std::ranges::sort(instances, [](const Instance &left, const Instance &right) {
                if (left.component.typeId != right.component.typeId)
                    return left.component.typeId < right.component.typeId;
                if (left.entity.entity.index != right.entity.entity.index)
                    return left.entity.entity.index < right.entity.entity.index;
                return left.component.instanceId < right.component.instanceId;
            });

            Runtime::SceneCommandBuffer commands;
            for (Instance &instance : instances) {
                ContextBackend backend{*this, instance, {}, commands, false};
                BehaviorContext context{backend};
                instance.implementation->OnCreate(context);
                instance.created = true;
                if (instance.component.enabled) {
                    instance.implementation->OnEnable(context);
                    instance.enabledCallbackActive = true;
                }
            }
            if (!commands.Empty()) {
                Result<Runtime::StructuralCommitResult> committed = scene.Commit(commands);
                if (committed.HasError())
                    return Result<void>::Failure(committed.ErrorValue());
            }
            return Result<void>::Success();
        }

        Runtime::RuntimeScene &scene;
        const BehaviorRegistry &registry;
        BehaviorRuntimeLimits limits;
        EventQueue events;
        std::vector<Instance> instances;
        bool shutdown{};
    };

    BehaviorRuntime::BehaviorRuntime(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}

    /** @copydoc BehaviorRuntime::Create */
    Result<std::unique_ptr<BehaviorRuntime>> BehaviorRuntime::Create(Runtime::RuntimeScene &scene, const BehaviorRegistry &registry,
                                                                     const BehaviorRuntimeLimits limits) {
        auto impl = std::make_unique<Impl>(scene, registry, limits);
        if (Result<void> built = impl->BuildInstances(); built.HasError()) {
            for (auto iterator = impl->instances.rbegin(); iterator != impl->instances.rend(); ++iterator) {
                if (iterator->created) {
                    Runtime::SceneCommandBuffer commands;
                    Impl::ContextBackend backend{*impl, *iterator, {}, commands, false};
                    BehaviorContext context{backend};
                    try {
                        if (iterator->enabledCallbackActive)
                            iterator->implementation->OnDisable(context);
                        iterator->implementation->OnDestroy(context);
                    } catch (
                        const std::exception &exception) {  // NOSONAR(cpp:S1181) User behavior code is an exception containment boundary.
                        LOG_WARN("gameplay.runtime", "Behavior rollback exception: %s", exception.what());
                    } catch (...) {
                        LOG_WARN("gameplay.runtime", "Behavior rollback unknown exception.");
                    }
                }
            }
            return Result<std::unique_ptr<BehaviorRuntime>>::Failure(built.ErrorValue());
        }
        return Result<std::unique_ptr<BehaviorRuntime>>::Success(std::unique_ptr<BehaviorRuntime>{new BehaviorRuntime{std::move(impl)}});
    }

    BehaviorRuntime::~BehaviorRuntime() {
        Shutdown();
    }

    /** @copydoc BehaviorRuntime::FixedUpdate */
    Result<void> BehaviorRuntime::FixedUpdate(const std::span<const GameplayInputAction> input, const FixedDeltaTime delta) {
        if (impl_->shutdown)
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorComponent, "Behavior runtime is shut down."));
        impl_->events.BeginTick();
        Runtime::SceneCommandBuffer commands;
        for (Impl::Instance &instance : impl_->instances) {
            if (!instance.component.enabled)
                continue;
            Impl::ContextBackend backend{*impl_, instance, input, commands};
            BehaviorContext context{backend};
            if (!instance.started) {
                instance.implementation->OnStart(context);
                instance.started = true;
            }
            for (const GameplayEvent &event : impl_->events.current) {
                if (!event.target || ToRuntime(*event.target) == instance.entity)
                    instance.implementation->OnEvent(context, event);
            }
            for (const GameplayInputAction &action : input) {
                if (action.pressed || action.released)
                    instance.implementation->OnInputAction(context, action);
            }
            instance.implementation->OnFixedUpdate(context, delta);
        }
        if (!commands.Empty()) {
            Result<Runtime::StructuralCommitResult> committed = impl_->scene.Commit(commands);
            if (committed.HasError())
                return Result<void>::Failure(committed.ErrorValue());
        }
        return Result<void>::Success();
    }

    /** @copydoc BehaviorRuntime::PresentationUpdate */
    void BehaviorRuntime::PresentationUpdate(const FrameDeltaTime delta) {
        if (impl_->shutdown)
            return;
        Runtime::SceneCommandBuffer rejectedCommands;
        for (Impl::Instance &instance : impl_->instances) {
            if (!instance.component.enabled)
                continue;
            Impl::ContextBackend backend{*impl_, instance, {}, rejectedCommands, false};
            BehaviorContext context{backend};
            instance.implementation->OnPresentationUpdate(context, delta);
        }
    }

    /** @copydoc BehaviorRuntime::SetEnabled */
    Result<void> BehaviorRuntime::SetEnabled(const BehaviorInstanceId instanceId, const bool enabled) {
        const auto found = std::ranges::find(impl_->instances, instanceId, [](const Impl::Instance &instance) {
            return instance.component.instanceId;
        });
        if (found == impl_->instances.end())
            return Result<void>::Failure(MakeError(GameplayErrors::InvalidBehaviorInstanceId));
        if (found->component.enabled == enabled)
            return Result<void>::Success();
        Runtime::SceneCommandBuffer commands;
        Impl::ContextBackend backend{*impl_, *found, {}, commands, false};
        BehaviorContext context{backend};
        if (enabled) {
            found->implementation->OnEnable(context);
            found->enabledCallbackActive = true;
        } else {
            found->implementation->OnDisable(context);
            found->enabledCallbackActive = false;
        }
        found->component.enabled = enabled;
        return Result<void>::Success();
    }

    /** @copydoc BehaviorRuntime::Shutdown */
    void BehaviorRuntime::Shutdown() noexcept {
        if (!impl_ || impl_->shutdown)
            return;
        impl_->shutdown = true;
        Runtime::SceneCommandBuffer commands;
        for (auto iterator = impl_->instances.rbegin(); iterator != impl_->instances.rend(); ++iterator) {
            Impl::ContextBackend backend{*impl_, *iterator, {}, commands, false};
            BehaviorContext context{backend};
            try {
                if (iterator->enabledCallbackActive)
                    iterator->implementation->OnDisable(context);
                iterator->implementation->OnDestroy(context);
            } catch (const std::exception &exception) {
                LOG_WARN("gameplay.runtime", "Behavior shutdown exception: %s", exception.what());
            } catch (...) {
                LOG_WARN("gameplay.runtime", "Behavior shutdown unknown exception.");
            }
        }
        impl_->instances.clear();  // Destroys each remaining instance through its owning unique_ptr.
        impl_->events.current.clear();
        impl_->events.next.clear();
    }

    /** @copydoc BehaviorRuntime::InstanceCount */
    std::size_t BehaviorRuntime::InstanceCount() const noexcept {
        return impl_->instances.size();
    }
}  // namespace Horo::Gameplay
