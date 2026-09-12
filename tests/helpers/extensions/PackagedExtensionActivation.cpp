#include "Horo/Extensions/ExtensionManager.h"
#include "SecurityTestSupport.h"

#include <string>

int main(const int argc, char **argv) {
    if (argc != 3)
        return 2;
    Horo::Extensions::ExtensionManager manager{nullptr,
                                               Horo::Extensions::ExtensionHostProfile::Interactive,
                                               {},
                                               Horo::Tests::CreateAcceptingArtifactGate()};
    const auto result = manager.LoadExtension(argv[1]);
    if (result.HasError() || result.Value() != argv[2])
        return 1;
    const auto loaded = manager.GetLoadedExtensionIds();
    return loaded.size() == 1 && loaded.front() == argv[2] ? 0 : 1;
}
