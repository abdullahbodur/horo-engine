#include "Horo/Foundation/CancellationToken.h"
#include "Horo/Foundation/Progress.h"

#include <cassert>

namespace
{

void ParentCancellationReachesChildToken()
{
    Horo::CancellationSource parent;
    Horo::CancellationSource child{parent.Token()};
    assert(!child.Token().IsCancellationRequested());

    parent.RequestCancellation();
    assert(child.Token().IsCancellationRequested());
}

void ChildCancellationDoesNotCancelParent()
{
    Horo::CancellationSource parent;
    Horo::CancellationSource child{parent.Token()};
    child.RequestCancellation();

    assert(child.Token().IsCancellationRequested());
    assert(!parent.Token().IsCancellationRequested());
}

void ProgressIsMonotonicWithinOnePhase()
{
    Horo::ProgressTracker progress;
    assert(progress.Report("compile", 0.25F));
    assert(progress.Report("compile", 0.75F));
    assert(!progress.Report("compile", 0.50F));
    assert(progress.Snapshot().phase == "compile");
    assert(progress.Snapshot().value == 0.75F);
}

void ProgressMayResetWhenPhaseAdvances()
{
    Horo::ProgressTracker progress;
    assert(progress.Report("compile", 1.0F));
    assert(progress.Report("package", 0.1F));
    assert(progress.Snapshot().phase == "package");
    assert(progress.Snapshot().value == 0.1F);
}

} // namespace

int main()
{
    ParentCancellationReachesChildToken();
    ChildCancellationDoesNotCancelParent();
    ProgressIsMonotonicWithinOnePhase();
    ProgressMayResetWhenPhaseAdvances();
    return 0;
}
