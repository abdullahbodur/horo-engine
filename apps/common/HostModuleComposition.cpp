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

        std::vector<ModuleDescriptor> modules{
            Describe("horo.foundation"),
            Describe("horo.application", {"horo.foundation"}),
            Describe("horo.application.project_migrations", {"horo.application"}),
            Describe("horo.platform", {"horo.foundation"}),
            Describe("horo.runtime", {"horo.foundation"}),
            Describe("horo.assets", {"horo.foundation"}),
            Describe("horo.input", {"horo.foundation"}),
            Describe("horo.input.sdl", {"horo.input"}),
            Describe("horo.gameplay.api", {"horo.foundation"}),
            Describe("horo.render.api", {"horo.foundation"}),
            Describe("horo.scene.model", {"horo.foundation", "horo.render.api"}),
            Describe("horo.runtime.scene", {"horo.foundation", "horo.runtime", "horo.assets", "horo.gameplay.api", "horo.scene.model"}),
            Describe("horo.gameplay.runtime", {"horo.gameplay.api", "horo.runtime.scene"}),
            Describe("horo.gameplay.module_host", {"horo.gameplay.runtime", "horo.platform"}),
            Describe("horo.gameplay.build", {"horo.foundation", "horo.platform", "horo.gameplay.module_host"}),
            Describe("horo.gameplay.lua", {"horo.gameplay.runtime"}),
            Describe("horo.render.backend_registry", {"horo.render.api"}),
            Describe("horo.render.frontend", {"horo.render.api", "horo.render.backend_registry"}),
            Describe("horo.editor.model", {"horo.foundation", "horo.scene.model", "horo.runtime.scene"}),
            Describe("horo.editor.viewport_scene", {"horo.editor.model"}),
            Describe("horo.editor.render_extraction", {"horo.editor.model", "horo.editor.viewport_scene"}),
            Describe("horo.editor.services",
                     {"horo.foundation", "horo.application", "horo.platform", "horo.editor.model", "horo.gameplay.module_host",
                      "horo.gameplay.build", "horo.gameplay.lua", "horo.input", "horo.application.project_migrations", "horo.assets"}),
            Describe("horo.extensions", {"horo.foundation", "horo.platform", "horo.assets"}),
            Describe("horo.gui", {"horo.editor.services", "horo.foundation", "horo.editor.render_extraction", "horo.extensions"}),
        };

        const bool useOpenGL = selection.renderer == HostRenderer::OpenGL;
        const std::string_view rendererModule = useOpenGL ? "horo.render.opengl" : "horo.render.metal";
        const std::string_view viewportModule = useOpenGL ? "horo.editor.viewport.opengl" : "horo.editor.viewport.metal";
        modules.push_back(Describe(std::string{rendererModule}, {"horo.render.backend_registry"}));
        modules.push_back(Describe(std::string{viewportModule}, {"horo.editor.viewport_scene", rendererModule}));
        if (selection.includeOpenTelemetry)
            modules.push_back(Describe("horo.observability.opentelemetry", {"horo.foundation"}));

        ModuleDescriptor host =
            Describe("horo.host.editor", {"horo.gui", "horo.editor.services", "horo.editor.render_extraction", "horo.render.frontend",
                                          "horo.runtime", "horo.runtime.scene", "horo.extensions", "horo.platform", "horo.input.sdl",
                                          "horo.application.project_migrations", viewportModule});
        if (selection.includeOpenTelemetry) {
            host.dependencies.push_back(
                ModuleDependency{.module = ModuleId{"horo.observability.opentelemetry"}, .minimumVersion = kContractVersion});
        }
        modules.push_back(std::move(host));
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
