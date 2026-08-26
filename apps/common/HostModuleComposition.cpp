#include "HostModuleComposition.h"

#include "Horo/Foundation/ErrorCode.h"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace Horo::Application::Internal {
    namespace {
        constexpr ModuleContractVersion kContractVersion{1, 0, 0};

        [[nodiscard]] ModuleDescriptor Describe(std::string id, const std::initializer_list<std::string_view> dependencies = {}) {
            ModuleDescriptor descriptor{.id = ModuleId{std::move(id)}, .version = kContractVersion};
            descriptor.dependencies.reserve(dependencies.size());
            for (const std::string_view dependency : dependencies) {
                descriptor.dependencies.push_back(
                    ModuleDependency{.module = ModuleId{std::string{dependency}}, .minimumVersion = kContractVersion});
            }
            return descriptor;
        }

        template <typename T> [[nodiscard]] Result<T> InvalidSelection(std::string message) {
            return Result<T>::Failure(Error{.code = ErrorCode{"application.host.invalid_module_selection"},
                                            .domain = ErrorDomainId{"horo.application.host"},
                                            .severity = ErrorSeverity::Critical,
                                            .message = std::move(message)});
        }

        void AddEditorFoundationModules(std::vector<ModuleDescriptor> &modules) {
            modules.push_back(Describe("horo.foundation"));
            modules.push_back(Describe("horo.application", {"horo.foundation"}));
            modules.push_back(Describe("horo.application.project_migrations", {"horo.application"}));
            modules.push_back(Describe("horo.platform", {"horo.foundation"}));
            modules.push_back(Describe("horo.runtime", {"horo.foundation"}));
            modules.push_back(Describe("horo.assets", {"horo.foundation"}));
            modules.push_back(Describe("horo.input", {"horo.foundation"}));
            modules.push_back(Describe("horo.input.sdl", {"horo.input"}));
        }

        void AddEditorGameplayModules(std::vector<ModuleDescriptor> &modules) {
            modules.push_back(Describe("horo.gameplay.api", {"horo.foundation"}));
            modules.push_back(Describe("horo.render.api", {"horo.foundation"}));
            modules.push_back(Describe("horo.scene.model", {"horo.foundation", "horo.render.api"}));
            modules.push_back(Describe("horo.runtime.scene",
                                       {"horo.foundation", "horo.runtime", "horo.assets", "horo.gameplay.api", "horo.scene.model"}));
            modules.push_back(Describe("horo.gameplay.runtime", {"horo.gameplay.api", "horo.runtime.scene"}));
            modules.push_back(Describe("horo.gameplay.module_host", {"horo.gameplay.runtime", "horo.platform"}));
            modules.push_back(Describe("horo.gameplay.build", {"horo.foundation", "horo.platform", "horo.gameplay.module_host"}));
            modules.push_back(Describe("horo.gameplay.lua", {"horo.gameplay.runtime"}));
        }

        void AddEditorRenderModules(std::vector<ModuleDescriptor> &modules, const HostRenderer renderer) {
            modules.push_back(Describe("horo.render.backend_registry", {"horo.render.api"}));
            modules.push_back(Describe("horo.render.frontend", {"horo.render.api", "horo.render.backend_registry"}));

            if (renderer == HostRenderer::OpenGL) {
                modules.push_back(Describe("horo.render.opengl", {"horo.render.backend_registry"}));
                modules.push_back(Describe("horo.editor.viewport.opengl", {"horo.editor.viewport_scene", "horo.render.opengl"}));
            } else {
                modules.push_back(Describe("horo.render.metal", {"horo.render.backend_registry"}));
                modules.push_back(Describe("horo.editor.viewport.metal", {"horo.editor.viewport_scene", "horo.render.metal"}));
            }
        }

        void AddEditorFeatureModules(std::vector<ModuleDescriptor> &modules) {
            modules.push_back(Describe("horo.editor.model", {"horo.foundation", "horo.scene.model", "horo.runtime.scene"}));
            modules.push_back(Describe("horo.editor.viewport_scene", {"horo.editor.model"}));
            modules.push_back(Describe("horo.editor.render_extraction", {"horo.editor.model", "horo.editor.viewport_scene"}));
            modules.push_back(Describe("horo.editor.services", {"horo.foundation", "horo.application", "horo.platform", "horo.editor.model",
                                                                "horo.gameplay.module_host", "horo.gameplay.build", "horo.gameplay.lua",
                                                                "horo.input", "horo.application.project_migrations", "horo.assets"}));
            modules.push_back(Describe("horo.extensions", {"horo.foundation", "horo.platform", "horo.assets"}));
            modules.push_back(
                Describe("horo.gui", {"horo.editor.services", "horo.foundation", "horo.editor.render_extraction", "horo.extensions"}));
        }

        void AddEditorHost(std::vector<ModuleDescriptor> &modules, const HostModuleSelection &selection) {
            std::vector<std::string_view> dependencies{
                "horo.gui",
                "horo.editor.services",
                "horo.editor.render_extraction",
                "horo.render.frontend",
                "horo.runtime",
                "horo.runtime.scene",
                "horo.extensions",
                "horo.platform",
                "horo.input.sdl",
                "horo.application.project_migrations",
            };
            dependencies.push_back(selection.renderer == HostRenderer::OpenGL ? "horo.editor.viewport.opengl"
                                                                              : "horo.editor.viewport.metal");
            if (selection.includeOpenTelemetry)
                dependencies.push_back("horo.observability.opentelemetry");

            ModuleDescriptor host = Describe("horo.host.editor");
            host.dependencies.reserve(dependencies.size());
            for (const std::string_view dependency : dependencies) {
                host.dependencies.push_back(
                    ModuleDependency{.module = ModuleId{std::string{dependency}}, .minimumVersion = kContractVersion});
            }
            modules.push_back(std::move(host));
        }
    }  // namespace

    /** @copydoc DescribeHostModules */
    Result<std::vector<ModuleDescriptor>> DescribeHostModules(const HostModuleSelection &selection) {
        if (selection.host == HostKind::Headless) {
            if (selection.renderer != HostRenderer::None || selection.includeOpenTelemetry) {
                return InvalidSelection<std::vector<ModuleDescriptor>>(
                    "The current headless host does not link an interactive renderer or OpenTelemetry module.");
            }
            std::vector<ModuleDescriptor> modules;
            modules.reserve(3);
            modules.push_back(Describe("horo.foundation"));
            modules.push_back(Describe("horo.application", {"horo.foundation"}));
            modules.push_back(Describe("horo.host.cli", {"horo.application"}));
            return Result<std::vector<ModuleDescriptor>>::Success(std::move(modules));
        }

        if (selection.renderer == HostRenderer::None) {
            return InvalidSelection<std::vector<ModuleDescriptor>>(
                "The graphical editor host requires one concrete interactive renderer module.");
        }

        std::vector<ModuleDescriptor> modules;
        modules.reserve(27);
        AddEditorFoundationModules(modules);
        AddEditorGameplayModules(modules);
        AddEditorFeatureModules(modules);
        AddEditorRenderModules(modules, selection.renderer);
        if (selection.includeOpenTelemetry)
            modules.push_back(Describe("horo.observability.opentelemetry", {"horo.foundation"}));
        AddEditorHost(modules, selection);
        return Result<std::vector<ModuleDescriptor>>::Success(std::move(modules));
    }

    /** @copydoc ComposeHostModules */
    Result<std::unique_ptr<ModuleHost>> ComposeHostModules(const HostModuleSelection &selection) {
        auto described = DescribeHostModules(selection);
        if (described.HasError())
            return Result<std::unique_ptr<ModuleHost>>::Failure(described.ErrorValue());

        auto host = std::make_unique<ModuleHost>();
        for (const ModuleDescriptor &descriptor : described.Value()) {
            if (const Result<void> registered = host->Register(descriptor); registered.HasError())
                return Result<std::unique_ptr<ModuleHost>>::Failure(registered.ErrorValue());
        }
        if (const Result<std::size_t> activated = host->ActivateRegistered(nullptr); activated.HasError())
            return Result<std::unique_ptr<ModuleHost>>::Failure(activated.ErrorValue());
        return Result<std::unique_ptr<ModuleHost>>::Success(std::move(host));
    }
}  // namespace Horo::Application::Internal
