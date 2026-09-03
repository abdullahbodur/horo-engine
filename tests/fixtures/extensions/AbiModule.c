#include "Horo/Extensions/ExtensionAbi.h"

#include <stddef.h>

static uint32_t loadCount;

/** @brief Expose whether negotiation prevented the load callback. */
HORO_EXTENSION_EXPORT uint32_t horo_test_load_count(void) {
    return loadCount;
}

#if HORO_ABI_FIXTURE_MODE != 0
/** @brief Return a valid or deliberately incompatible inert requirements table. */
HORO_EXTENSION_EXPORT HoroExtensionStatus horo_extension_query(HoroExtensionRequirements *requirements) {
    *requirements = (HoroExtensionRequirements){.structSize = sizeof(HoroExtensionRequirements),
                                                .abiMajorVersion = HORO_EXTENSION_ABI_VERSION,
                                                .minimumHostMinor = HORO_ABI_FIXTURE_MODE == 2 ? 99 : 0,
                                                .requiredHostApiSize = offsetof(HoroExtensionHostApi, abiMinorVersion)};
    return HORO_EXTENSION_SUCCESS;
}
#endif

/** @brief Return a legacy module prefix or a deliberately truncated result. */
HORO_EXTENSION_EXPORT HoroExtensionStatus horo_extension_load(const HoroExtensionHostApi *host, HoroExtensionModuleApi *outModule) {
    (void)host;
    ++loadCount;
    outModule->structSize = HORO_ABI_FIXTURE_MODE == 3 ? 1 : offsetof(HoroExtensionModuleApi, moduleId);
    return HORO_EXTENSION_SUCCESS;
}
