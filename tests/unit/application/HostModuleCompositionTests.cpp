#include "HostModuleComposition.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

namespace {
    using namespace Horo;
    using namespace Horo::Application::Internal;

    [[nodiscard]] std::vector<std::string> ModuleIds(const std::vector<ModuleDescriptor> &descriptors) {
        std::vector<std::string> ids;
        ids.reserve(descriptors.size());
        for (const ModuleDescriptor &descriptor : descriptors)
            ids.push_back(descriptor.id.value);
        return ids;
    }
}  // namespace

TEST_CASE("Headless host composition activates only its linked module set", "[unit][application][modules]") {
    const HostModuleSelection selection{.host = HostKind::Headless};
    auto described = DescribeHostModules(selection);
    REQUIRE(described.HasValue());
    REQUIRE(ModuleIds(described.Value()) == std::vector<std::string>{"horo.foundation", "horo.application", "horo.host.cli"});

    auto composed = ComposeHostModules(selection);
    INFO((composed.HasError() ? composed.ErrorValue().message : std::string{}));
    REQUIRE(composed.HasValue());
    REQUIRE(composed.Value()->HasActiveModules());
    REQUIRE(composed.Value()->StateOf(ModuleId{"horo.host.cli"}) == ModuleLifecycleState::Active);
    REQUIRE_FALSE(composed.Value()->StateOf(ModuleId{"horo.gui"}).has_value());
    REQUIRE_FALSE(composed.Value()->StateOf(ModuleId{"horo.render.opengl"}).has_value());
    REQUIRE_FALSE(composed.Value()->StateOf(ModuleId{"horo.render.metal"}).has_value());
}

TEST_CASE("Editor host composition selects exactly one concrete renderer", "[unit][application][modules]") {
    const HostModuleSelection selection{.host = HostKind::Editor, .renderer = HostRenderer::OpenGL, .includeOpenTelemetry = true};
    auto composed = ComposeHostModules(selection);
    INFO((composed.HasError() ? composed.ErrorValue().message : std::string{}));
    REQUIRE(composed.HasValue());
    REQUIRE(composed.Value()->StateOf(ModuleId{"horo.host.editor"}) == ModuleLifecycleState::Active);
    REQUIRE(composed.Value()->StateOf(ModuleId{"horo.render.opengl"}) == ModuleLifecycleState::Active);
    REQUIRE(composed.Value()->StateOf(ModuleId{"horo.editor.viewport.opengl"}) == ModuleLifecycleState::Active);
    REQUIRE(composed.Value()->StateOf(ModuleId{"horo.observability.opentelemetry"}) == ModuleLifecycleState::Active);
    REQUIRE_FALSE(composed.Value()->StateOf(ModuleId{"horo.render.metal"}).has_value());
    REQUIRE_FALSE(composed.Value()->StateOf(ModuleId{"horo.editor.viewport.metal"}).has_value());
}

TEST_CASE("Supported host descriptor order is deterministic from the same selection", "[unit][application][modules]") {
    const HostModuleSelection selection{.host = HostKind::Editor, .renderer = HostRenderer::Metal};
    auto first = DescribeHostModules(selection);
    auto second = DescribeHostModules(selection);
    REQUIRE(first.HasValue());
    REQUIRE(second.HasValue());

    auto firstGraph = ValidateModuleGraph(first.Value());
    INFO((firstGraph.HasError() ? firstGraph.ErrorValue().message : std::string{}));
    REQUIRE(firstGraph.HasValue());
    std::vector<ModuleDescriptor> reversed = second.Value();
    std::ranges::reverse(reversed);
    auto secondGraph = ValidateModuleGraph(reversed);
    REQUIRE(secondGraph.HasValue());
    REQUIRE(firstGraph.Value().initializationOrder == secondGraph.Value().initializationOrder);
}

TEST_CASE("Impossible host module selections fail before activation", "[unit][application][modules]") {
    REQUIRE(DescribeHostModules({.host = HostKind::Headless, .renderer = HostRenderer::OpenGL}).HasError());
    REQUIRE(DescribeHostModules({.host = HostKind::Headless, .includeOpenTelemetry = true}).HasError());
    REQUIRE(DescribeHostModules({.host = HostKind::Editor, .renderer = HostRenderer::None}).HasError());
}
