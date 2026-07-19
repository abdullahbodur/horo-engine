#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/DataBus.h"
#include "Horo/Foundation/DiagnosticsEngine.h"

#include <string>

namespace
{
TEST_CASE("Report Diagnostic And Notify Data Bus", "[unit][foundation]")
{
    Horo::EngineDataBus bus{Horo::EngineDataBusConfig{.traceDispatch = false}};
    Horo::DiagnosticsEngine engine(&bus);

    Horo::Diagnostic observedDiag{};
    bool receivedDiag = false;
    const Horo::Subscription diagSub =
        bus.Subscribe<Horo::DiagnosticPublishedEvent>([&](const Horo::DiagnosticPublishedEvent &event) {
            observedDiag = event.diagnostic;
            receivedDiag = true;
        });

    engine.Report(Horo::Diagnostic{.code = Horo::DiagnosticCode{"test.diag_code"},
                                   .severity = Horo::DiagnosticSeverity::Warning,
                                   .message = "A warning diagnostic",
                                   .location = Horo::SourceLocation{"config.json", 12, 4}});

    bus.DispatchQueued();
    REQUIRE((receivedDiag));
    REQUIRE((observedDiag.code.Value() == "test.diag_code"));
    REQUIRE((observedDiag.message == "A warning diagnostic"));
    REQUIRE((observedDiag.severity == Horo::DiagnosticSeverity::Warning));

    const auto recent = engine.RecentDiagnostics();
    REQUIRE((recent.size() == 1));
    REQUIRE((recent.front().message == "A warning diagnostic"));
}

TEST_CASE("Report Error And Notify Data Bus", "[unit][foundation]")
{
    Horo::EngineDataBus bus{Horo::EngineDataBusConfig{.traceDispatch = false}};
    Horo::DiagnosticsEngine engine(&bus);

    Horo::Error observedErr{};
    bool receivedErr = false;
    int receivedDiagsCount = 0;

    const Horo::Subscription errSub =
        bus.Subscribe<Horo::ErrorPublishedEvent>([&](const Horo::ErrorPublishedEvent &event) {
            observedErr = event.error;
            receivedErr = true;
        });
    const Horo::Subscription diagSub = bus.Subscribe<Horo::DiagnosticPublishedEvent>(
        [&](const Horo::DiagnosticPublishedEvent &) { receivedDiagsCount++; });

    Horo::Error err{
        .code = Horo::ErrorCode{"test.err_code"},
        .domain = Horo::ErrorDomainId{"horo.test"},
        .severity = Horo::ErrorSeverity::Critical,
        .message = "Critical failure occurred",
        .diagnostics = {Horo::Diagnostic{.code = Horo::DiagnosticCode{"diag1"}, .message = "First nested diag"},
                        Horo::Diagnostic{.code = Horo::DiagnosticCode{"diag2"}, .message = "Second nested diag"}}};

    engine.Report(std::move(err));
    bus.DispatchQueued();

    REQUIRE((receivedErr));
    REQUIRE((observedErr.code.Value() == "test.err_code"));
    REQUIRE((receivedDiagsCount == 2));

    REQUIRE((engine.RecentErrors().size() == 1));
    REQUIRE((engine.RecentDiagnostics().size() == 2));

    engine.Clear();
    REQUIRE((engine.RecentErrors().empty()));
    REQUIRE((engine.RecentDiagnostics().empty()));
}
} // namespace
