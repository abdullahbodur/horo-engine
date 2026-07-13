#include "Horo/Foundation/DiagnosticsEngine.h"
#include "Horo/Foundation/DataBus.h"

#include <cassert>
#include <string>

namespace
{
    void ReportDiagnosticAndNotifyDataBus()
    {
        Horo::EngineDataBus bus{Horo::EngineDataBusConfig{.traceDispatch = false}};
        Horo::DiagnosticsEngine engine(&bus);

        Horo::Diagnostic observedDiag{};
        bool receivedDiag = false;
        const Horo::Subscription diagSub = bus.Subscribe<Horo::DiagnosticPublishedEvent>([&](const Horo::DiagnosticPublishedEvent &event) {
            observedDiag = event.diagnostic;
            receivedDiag = true;
        });

        engine.Report(Horo::Diagnostic{
            .code = Horo::DiagnosticCode{"test.diag_code"},
            .severity = Horo::DiagnosticSeverity::Warning,
            .message = "A warning diagnostic",
            .location = Horo::SourceLocation{"config.json", 12, 4}
        });

        bus.DispatchQueued();
        assert(receivedDiag);
        assert(observedDiag.code.Value() == "test.diag_code");
        assert(observedDiag.message == "A warning diagnostic");
        assert(observedDiag.severity == Horo::DiagnosticSeverity::Warning);

        const auto recent = engine.RecentDiagnostics();
        assert(recent.size() == 1);
        assert(recent.front().message == "A warning diagnostic");
    }

    void ReportErrorAndNotifyDataBus()
    {
        Horo::EngineDataBus bus{Horo::EngineDataBusConfig{.traceDispatch = false}};
        Horo::DiagnosticsEngine engine(&bus);

        Horo::Error observedErr{};
        bool receivedErr = false;
        int receivedDiagsCount = 0;

        const Horo::Subscription errSub = bus.Subscribe<Horo::ErrorPublishedEvent>([&](const Horo::ErrorPublishedEvent &event) {
            observedErr = event.error;
            receivedErr = true;
        });
        const Horo::Subscription diagSub = bus.Subscribe<Horo::DiagnosticPublishedEvent>([&](const Horo::DiagnosticPublishedEvent &) {
            receivedDiagsCount++;
        });

        Horo::Error err{
            .code = Horo::ErrorCode{"test.err_code"},
            .domain = Horo::ErrorDomainId{"horo.test"},
            .severity = Horo::ErrorSeverity::Critical,
            .message = "Critical failure occurred",
            .diagnostics = {
                Horo::Diagnostic{.code = Horo::DiagnosticCode{"diag1"}, .message = "First nested diag"},
                Horo::Diagnostic{.code = Horo::DiagnosticCode{"diag2"}, .message = "Second nested diag"}
            }
        };

        engine.Report(std::move(err));
        bus.DispatchQueued();

        assert(receivedErr);
        assert(observedErr.code.Value() == "test.err_code");
        assert(receivedDiagsCount == 2);

        assert(engine.RecentErrors().size() == 1);
        assert(engine.RecentDiagnostics().size() == 2);

        engine.Clear();
        assert(engine.RecentErrors().empty());
        assert(engine.RecentDiagnostics().empty());
    }
} // namespace

int main()
{
    ReportDiagnosticAndNotifyDataBus();
    ReportErrorAndNotifyDataBus();
    return 0;
}
