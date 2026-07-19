#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Progress.h"

namespace
{
TEST_CASE("Parent Cancellation Reaches Child Token", "[unit][foundation]")
{
    Horo::CancellationSource parent;
    Horo::CancellationSource child{parent.Token()};
    REQUIRE((!child.Token().IsCancellationRequested()));

    parent.RequestCancellation();
    REQUIRE((child.Token().IsCancellationRequested()));
}

TEST_CASE("Child Cancellation Does Not Cancel Parent", "[unit][foundation]")
{
    Horo::CancellationSource parent;
    Horo::CancellationSource child{parent.Token()};
    child.RequestCancellation();

    REQUIRE((child.Token().IsCancellationRequested()));
    REQUIRE((!parent.Token().IsCancellationRequested()));
}

TEST_CASE("Progress Is Monotonic Within One Phase", "[unit][foundation]")
{
    Horo::ProgressTracker progress;
    REQUIRE((progress.Report("compile", 0.25F)));
    REQUIRE((progress.Report("compile", 0.75F)));
    REQUIRE((!progress.Report("compile", 0.50F)));
    REQUIRE((progress.Snapshot().phase == "compile"));
    REQUIRE((progress.Snapshot().value == 0.75F));
}

TEST_CASE("Progress May Reset When Phase Advances", "[unit][foundation]")
{
    Horo::ProgressTracker progress;
    REQUIRE((progress.Report("compile", 1.0F)));
    REQUIRE((progress.Report("package", 0.1F)));
    REQUIRE((progress.Snapshot().phase == "package"));
    REQUIRE((progress.Snapshot().value == 0.1F));
}
} // namespace
