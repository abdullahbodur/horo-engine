#include "Horo/Gameplay/GameServiceRegistry.h"
#include "Horo/Gameplay/GameplayErrors.h"
#include "Horo/Gameplay/GameplayRegistrationRuntime.h"
#include "Horo/Gameplay/SystemRegistry.h"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Gameplay;

    struct FactoryProbe {
        std::string name;
        std::vector<std::string> *events{};
        bool failStart{};
        bool failExecute{};
        bool returnNull{};
        bool throwOnCreate{};
    };

    class RecordingService final : public IGameplayService {
    public:
        explicit RecordingService(FactoryProbe &probe) : probe_(probe) {}

        Result<void> Start(const GameplayServiceContext &context) override {
            probe_.events->push_back("start:" + probe_.name);
            REQUIRE_FALSE(context.cancellation.IsCancellationRequested());
            if (probe_.failStart)
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed));
            return Result<void>::Success();
        }

        void Stop(const GameplayServiceContext &context) noexcept override {
            probe_.events->push_back("stop:" + probe_.name + (context.cancellation.IsCancellationRequested() ? ":cancelled" : ":live"));
        }

    private:
        FactoryProbe &probe_;
    };

    class RecordingSystem final : public IGameplaySystem {
    public:
        explicit RecordingSystem(FactoryProbe &probe) : probe_(probe) {}

        Result<void> Start(const GameplaySystemContext &context) override {
            probe_.events->push_back("start:" + probe_.name);
            REQUIRE(context.thread == GameplayThreadAffinity::RuntimeOwner);
            if (probe_.failStart)
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed));
            return Result<void>::Success();
        }

        Result<void> Execute(const GameplaySystemContext &context) override {
            probe_.events->push_back("execute:" + probe_.name);
            REQUIRE(context.deltaSeconds > 0.0);
            if (probe_.failExecute)
                return Result<void>::Failure(MakeError(GameplayErrors::GameplayFactoryFailed));
            return Result<void>::Success();
        }

        void Stop(const GameplaySystemContext &context) noexcept override {
            probe_.events->push_back("stop:" + probe_.name + (context.cancellation.IsCancellationRequested() ? ":cancelled" : ":live"));
        }

    private:
        FactoryProbe &probe_;
    };

    IGameplayService *CreateService(void *userData) {
        auto &probe = *static_cast<FactoryProbe *>(userData);
        if (probe.throwOnCreate)
            throw std::runtime_error{"service factory failure"};
        return probe.returnNull ? nullptr : new RecordingService{probe};
    }

    void DestroyService(void *userData, IGameplayService *service) noexcept {
        auto &probe = *static_cast<FactoryProbe *>(userData);
        probe.events->push_back("destroy:" + probe.name);
        delete service;
    }

    IGameplaySystem *CreateSystem(void *userData) {
        auto &probe = *static_cast<FactoryProbe *>(userData);
        if (probe.throwOnCreate)
            throw std::runtime_error{"system factory failure"};
        return probe.returnNull ? nullptr : new RecordingSystem{probe};
    }

    void DestroySystem(void *userData, IGameplaySystem *system) noexcept {
        auto &probe = *static_cast<FactoryProbe *>(userData);
        probe.events->push_back("destroy:" + probe.name);
        delete system;
    }

    GameplayServiceRegistration Service(const std::string_view id, FactoryProbe &probe,
                                        const GameplayServiceScope scope = GameplayServiceScope::Project) {
        return {
            .descriptor =
                {
                    .id = GameplayServiceId::Parse(id).Value(),
                    .scope = scope,
                    .affinity = GameplayThreadAffinity::RuntimeOwner,
                    .sceneReplacement = scope == GameplayServiceScope::Project ? GameplaySceneReplacementPolicy::Preserve
                                                                               : GameplaySceneReplacementPolicy::Restart,
                    .observabilityCategory = "game.tests.services",
                },
            .factory = {.userData = &probe, .create = &CreateService, .destroy = &DestroyService},
        };
    }

    GameplaySystemRegistration System(const std::string_view id, FactoryProbe &probe,
                                      const GameplaySystemPhase phase = GameplaySystemPhase::Gameplay) {
        return {
            .descriptor =
                {
                    .id = GameplaySystemId::Parse(id).Value(),
                    .phase = phase,
                    .affinity = GameplayThreadAffinity::RuntimeOwner,
                },
            .factory = {.userData = &probe, .create = &CreateSystem, .destroy = &DestroySystem},
        };
    }

    std::vector<GameplayServiceId> ServiceIds(const GameServiceRegistry &registry) {
        std::vector<GameplayServiceId> ids;
        for (const GameplayServiceRegistration &registration : registry.Registrations())
            ids.push_back(registration.descriptor.id);
        return ids;
    }
}  // namespace

TEST_CASE("gameplay registration identities are stable locale-independent values") {
    REQUIRE(GameplaySystemId::Parse("game.tests.movement_system").HasValue());
    REQUIRE(GameplayServiceId::Parse("game.tests.session_service").HasValue());
    REQUIRE(GameplayCapabilityId::Parse("cinematic.playback.start").HasValue());
    REQUIRE(GameplaySystemId::Parse("game.tests.Movement").HasError());
    REQUIRE(GameplayServiceId::Parse("engine.tests.service").HasError());
    REQUIRE(GameplayCapabilityId::Parse("game.tests.mövëment").HasError());
}

TEST_CASE("gameplay service registry freezes a deterministic provider-first dependency graph") {
    std::vector<std::string> events;
    FactoryProbe provider{"provider", &events};
    FactoryProbe consumer{"consumer", &events};
    GameServiceRegistry registry{"game.tests"};

    auto consumerRegistration = Service("game.tests.consumer", consumer);
    consumerRegistration.descriptor.dependencies = {GameplayServiceId::Parse("game.tests.provider").Value()};
    consumerRegistration.descriptor.requiredCapabilities = {GameplayCapabilityId::Parse("game.tests.provider.read").Value()};
    auto providerRegistration = Service("game.tests.provider", provider);
    providerRegistration.descriptor.providedCapabilities = {GameplayCapabilityId::Parse("game.tests.provider.read").Value()};

    REQUIRE(registry.Register(std::move(consumerRegistration)).HasValue());
    REQUIRE(registry.Register(std::move(providerRegistration)).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    REQUIRE(registry.IsFrozen());
    REQUIRE(registry.Registrations()[0].descriptor.id.Value() == "game.tests.provider");
    REQUIRE(registry.Registrations()[1].descriptor.id.Value() == "game.tests.consumer");
    REQUIRE(registry.ProvidedCapabilities().size() == 1);
    REQUIRE(registry.Register(Service("game.tests.late", provider)).ErrorValue().code.Value() ==
            GameplayErrors::RegistrationRegistryFrozen.code.Value());
}

TEST_CASE("gameplay service registry rejects missing cyclic ambiguous and invalid-lifetime dependencies") {
    std::vector<std::string> events;
    FactoryProbe first{"first", &events};
    FactoryProbe second{"second", &events};

    SECTION("missing dependency") {
        GameServiceRegistry registry{"game.tests"};
        auto registration = Service("game.tests.first", first);
        registration.descriptor.dependencies = {GameplayServiceId::Parse("game.tests.missing").Value()};
        REQUIRE(registry.Register(std::move(registration)).HasValue());
        REQUIRE(registry.Freeze().ErrorValue().code.Value() == GameplayErrors::ServiceDependencyMissing.code.Value());
    }

    SECTION("cycle") {
        GameServiceRegistry registry{"game.tests"};
        auto a = Service("game.tests.first", first);
        auto b = Service("game.tests.second", second);
        a.descriptor.dependencies = {b.descriptor.id};
        b.descriptor.dependencies = {a.descriptor.id};
        REQUIRE(registry.Register(std::move(a)).HasValue());
        REQUIRE(registry.Register(std::move(b)).HasValue());
        REQUIRE(registry.Freeze().ErrorValue().code.Value() == GameplayErrors::ServiceDependencyCycle.code.Value());
    }

    SECTION("project service cannot depend on scene scope") {
        GameServiceRegistry registry{"game.tests"};
        auto project = Service("game.tests.first", first);
        auto scene = Service("game.tests.second", second, GameplayServiceScope::Scene);
        project.descriptor.dependencies = {scene.descriptor.id};
        REQUIRE(registry.Register(std::move(project)).HasValue());
        REQUIRE(registry.Register(std::move(scene)).HasValue());
        REQUIRE(registry.Freeze().ErrorValue().code.Value() == GameplayErrors::ServiceScopeViolation.code.Value());
    }

    SECTION("capability providers are unique") {
        GameServiceRegistry registry{"game.tests"};
        const GameplayCapabilityId capability = GameplayCapabilityId::Parse("game.tests.shared.read").Value();
        auto a = Service("game.tests.first", first);
        auto b = Service("game.tests.second", second);
        a.descriptor.providedCapabilities = {capability};
        b.descriptor.providedCapabilities = {capability};
        REQUIRE(registry.Register(std::move(a)).HasValue());
        REQUIRE(registry.Register(std::move(b)).HasValue());
        REQUIRE(registry.Freeze().ErrorValue().code.Value() == GameplayErrors::InvalidServiceDescriptor.code.Value());
    }
}

TEST_CASE("gameplay system registry validates deterministic ordering access and requirements") {
    std::vector<std::string> events;
    FactoryProbe writer{"writer", &events};
    FactoryProbe reader{"reader", &events};
    const ComponentTypeId position = ComponentTypeId::Parse("game.tests.position").Value();
    const GameplayServiceId service = GameplayServiceId::Parse("game.tests.session_service").Value();
    const GameplayCapabilityId capability = GameplayCapabilityId::Parse("game.tests.session.read").Value();
    SystemRegistry registry{"game.tests"};

    auto read = System("game.tests.reader", reader);
    read.descriptor.access.reads = {position};
    read.descriptor.after = {GameplaySystemId::Parse("game.tests.writer").Value()};
    read.descriptor.requiredServices = {service};
    read.descriptor.requiredCapabilities = {capability};
    auto write = System("game.tests.writer", writer);
    write.descriptor.access.writes = {position};

    REQUIRE(registry.Register(std::move(read)).HasValue());
    REQUIRE(registry.Register(std::move(write)).HasValue());
    REQUIRE(registry.Freeze({&service, 1}, {&capability, 1}).HasValue());
    REQUIRE(registry.Registrations()[0].descriptor.id.Value() == "game.tests.writer");
    REQUIRE(registry.Registrations()[1].descriptor.id.Value() == "game.tests.reader");
}

TEST_CASE("gameplay system registry rejects unordered writes cycles missing requirements and presentation mutation") {
    std::vector<std::string> events;
    FactoryProbe first{"first", &events};
    FactoryProbe second{"second", &events};
    const ComponentTypeId position = ComponentTypeId::Parse("game.tests.position").Value();

    SECTION("unordered write conflict") {
        SystemRegistry registry{"game.tests"};
        auto a = System("game.tests.first", first);
        auto b = System("game.tests.second", second);
        a.descriptor.access.writes = {position};
        b.descriptor.access.reads = {position};
        REQUIRE(registry.Register(std::move(a)).HasValue());
        REQUIRE(registry.Register(std::move(b)).HasValue());
        REQUIRE(registry.Freeze({}, {}).ErrorValue().code.Value() == GameplayErrors::SystemAccessConflict.code.Value());
    }

    SECTION("schedule cycle") {
        SystemRegistry registry{"game.tests"};
        auto a = System("game.tests.first", first);
        auto b = System("game.tests.second", second);
        a.descriptor.after = {b.descriptor.id};
        b.descriptor.after = {a.descriptor.id};
        REQUIRE(registry.Register(std::move(a)).HasValue());
        REQUIRE(registry.Register(std::move(b)).HasValue());
        REQUIRE(registry.Freeze({}, {}).ErrorValue().code.Value() == GameplayErrors::SystemScheduleCycle.code.Value());
    }

    SECTION("missing capability") {
        SystemRegistry registry{"game.tests"};
        auto system = System("game.tests.first", first);
        system.descriptor.requiredCapabilities = {GameplayCapabilityId::Parse("game.tests.missing.read").Value()};
        REQUIRE(registry.Register(std::move(system)).HasValue());
        REQUIRE(registry.Freeze({}, {}).ErrorValue().code.Value() == GameplayErrors::CapabilityMissing.code.Value());
    }

    SECTION("presentation phase cannot write simulation data") {
        SystemRegistry registry{"game.tests"};
        auto system = System("game.tests.first", first, GameplaySystemPhase::Presentation);
        system.descriptor.access.writes = {position};
        REQUIRE(registry.Register(std::move(system)).ErrorValue().code.Value() == GameplayErrors::InvalidSystemDescriptor.code.Value());
    }
}

TEST_CASE("gameplay service runtime starts providers first and cancels before reverse shutdown") {
    std::vector<std::string> events;
    FactoryProbe provider{"provider", &events};
    FactoryProbe consumer{"consumer", &events};
    GameServiceRegistry registry{"game.tests"};
    auto dependent = Service("game.tests.consumer", consumer);
    dependent.descriptor.dependencies = {GameplayServiceId::Parse("game.tests.provider").Value()};
    auto dependency = Service("game.tests.provider", provider);
    REQUIRE(registry.Register(std::move(dependent)).HasValue());
    REQUIRE(registry.Register(std::move(dependency)).HasValue());
    REQUIRE(registry.Freeze().HasValue());

    auto runtime = GameplayServiceRuntime::Create(registry, GameplayServiceScope::Project);
    REQUIRE(runtime.HasValue());
    REQUIRE(runtime.Value()->InstanceCount() == 2);
    REQUIRE(events == std::vector<std::string>{"start:provider", "start:consumer"});
    runtime.Value()->Shutdown();
    REQUIRE(events == std::vector<std::string>{"start:provider", "start:consumer", "stop:consumer:cancelled", "destroy:consumer",
                                               "stop:provider:cancelled", "destroy:provider"});
}

TEST_CASE("gameplay service runtime rolls back failed and throwing factories without leaking instances") {
    std::vector<std::string> events;
    FactoryProbe provider{"provider", &events};
    FactoryProbe failingMiddle{"middle", &events, true};
    FactoryProbe consumer{"consumer", &events};
    GameServiceRegistry registry{"game.tests"};
    auto middle = Service("game.tests.middle", failingMiddle);
    middle.descriptor.dependencies = {GameplayServiceId::Parse("game.tests.provider").Value()};
    auto downstream = Service("game.tests.consumer", consumer);
    downstream.descriptor.dependencies = {middle.descriptor.id};
    REQUIRE(registry.Register(Service("game.tests.provider", provider)).HasValue());
    REQUIRE(registry.Register(std::move(downstream)).HasValue());
    REQUIRE(registry.Register(std::move(middle)).HasValue());
    REQUIRE(registry.Freeze().HasValue());
    REQUIRE(GameplayServiceRuntime::Create(registry, GameplayServiceScope::Project).HasError());
    REQUIRE(events == std::vector<std::string>{"start:provider", "start:middle", "stop:middle:cancelled", "destroy:middle",
                                               "stop:provider:cancelled", "destroy:provider"});

    events.clear();
    FactoryProbe throwing{"throwing", &events, false, false, false, true};
    GameServiceRegistry throwingRegistry{"game.tests"};
    REQUIRE(throwingRegistry.Register(Service("game.tests.throwing", throwing)).HasValue());
    REQUIRE(throwingRegistry.Freeze().HasValue());
    REQUIRE(GameplayServiceRuntime::Create(throwingRegistry, GameplayServiceScope::Project).ErrorValue().code.Value() ==
            GameplayErrors::GameplayFactoryFailed.code.Value());
    REQUIRE(events.empty());
}

TEST_CASE("gameplay system runtime dispatches validated order and enforces affinity cancellation and reverse shutdown") {
    std::vector<std::string> events;
    FactoryProbe first{"first", &events};
    FactoryProbe second{"second", &events};
    SystemRegistry registry{"game.tests"};
    auto dependant = System("game.tests.second", second);
    dependant.descriptor.after = {GameplaySystemId::Parse("game.tests.first").Value()};
    REQUIRE(registry.Register(std::move(dependant)).HasValue());
    REQUIRE(registry.Register(System("game.tests.first", first)).HasValue());
    REQUIRE(registry.Freeze({}, {}).HasValue());

    auto runtime = GameplaySystemRuntime::Create(registry, {}, {});
    REQUIRE(runtime.HasValue());
    REQUIRE(events == std::vector<std::string>{"start:first", "start:second"});
    REQUIRE(runtime.Value()->Execute(GameplaySystemPhase::Gameplay, GameplayThreadAffinity::Worker, 1.0 / 60.0).ErrorValue().code.Value() ==
            GameplayErrors::GameplayThreadAccessViolation.code.Value());
    REQUIRE(runtime.Value()->Execute(GameplaySystemPhase::Gameplay, GameplayThreadAffinity::RuntimeOwner, 1.0 / 60.0).HasValue());
    REQUIRE(events == std::vector<std::string>{"start:first", "start:second", "execute:first", "execute:second"});
    runtime.Value()->RequestCancellation();
    REQUIRE(runtime.Value()
                ->Execute(GameplaySystemPhase::Gameplay, GameplayThreadAffinity::RuntimeOwner, 1.0 / 60.0)
                .ErrorValue()
                .code.Value() == GameplayErrors::GameplayCancelled.code.Value());
    runtime.Value()->Shutdown();
    REQUIRE(events == std::vector<std::string>{"start:first", "start:second", "execute:first", "execute:second", "stop:second:cancelled",
                                               "destroy:second", "stop:first:cancelled", "destroy:first"});
}
