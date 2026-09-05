include_guard(GLOBAL)

# This manifest is intentionally explicit: changing a first-party link edge must
# update the architecture policy in the same review.
horo_allow_target_dependencies(TARGET HoroFoundation)
horo_allow_target_dependencies(TARGET HoroOpenTelemetry DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroPlatform DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroPackages DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroApplication DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroProjectMigrations DEPENDENCIES HoroApplication)
horo_allow_target_dependencies(TARGET HoroRuntime DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroAssets DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroAudioApi DEPENDENCIES HoroFoundation HoroAssets)
horo_allow_target_dependencies(TARGET HoroAudioDsp DEPENDENCIES HoroAudioApi)
horo_allow_target_dependencies(TARGET HoroAudioMemory DEPENDENCIES HoroAudioApi)
horo_allow_target_dependencies(TARGET HoroAudioCommands DEPENDENCIES HoroAudioMemory)
horo_allow_target_dependencies(TARGET HoroAudioBackendContract DEPENDENCIES HoroAudioApi)
horo_allow_target_dependencies(TARGET HoroInput DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroInputSdl DEPENDENCIES HoroInput)

horo_allow_target_dependencies(TARGET HoroGameplayApi DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroRuntimeScene
    DEPENDENCIES HoroFoundation HoroRuntime HoroAssets HoroGameplayApi HoroSceneModel)
horo_allow_target_dependencies(TARGET HoroGameplayRuntime
    DEPENDENCIES HoroGameplayApi HoroRuntimeScene)
horo_allow_target_dependencies(TARGET HoroGameplayModuleHost
    DEPENDENCIES HoroGameplayRuntime HoroPlatform)
horo_allow_target_dependencies(TARGET HoroGameplayBuild
    DEPENDENCIES HoroFoundation HoroPlatform HoroGameplayModuleHost)
horo_allow_target_dependencies(TARGET HoroGameplayLua DEPENDENCIES HoroGameplayRuntime)

horo_allow_target_dependencies(TARGET HoroRenderApi DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroRenderBackendRegistry DEPENDENCIES HoroRenderApi)
horo_allow_target_dependencies(TARGET HoroRenderFrontend
    DEPENDENCIES HoroRenderApi HoroRenderBackendRegistry)
horo_allow_target_dependencies(TARGET HoroSceneModel DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET HoroRenderNull DEPENDENCIES HoroRenderApi)
horo_allow_target_dependencies(TARGET HoroRenderOpenGL)
horo_allow_target_dependencies(TARGET HoroRenderMetal)

horo_allow_target_dependencies(TARGET HoroEditorModel
    DEPENDENCIES HoroFoundation HoroSceneModel HoroRuntimeScene)
horo_allow_target_dependencies(TARGET HoroEditorViewportScene DEPENDENCIES HoroEditorModel)
horo_allow_target_dependencies(TARGET HoroEditorViewportResources
    DEPENDENCIES HoroEditorViewportScene HoroRenderFrontend)
horo_allow_target_dependencies(TARGET HoroEditorRenderExtraction
    DEPENDENCIES HoroEditorModel HoroEditorViewportScene)
horo_allow_target_dependencies(TARGET HoroEditorServices
    DEPENDENCIES
        HoroFoundation
        HoroApplication
        HoroPlatform
        HoroEditorModel
        HoroGameplayModuleHost
        HoroGameplayBuild
        HoroInput
        HoroProjectMigrations
        HoroAssets)
horo_allow_target_dependencies(TARGET HoroEditorViewportOpenGL
    DEPENDENCIES HoroEditorViewportScene HoroEditorViewportResources HoroRenderOpenGL HoroRenderFrontend)
horo_allow_target_dependencies(TARGET HoroEditorViewportMetal
    DEPENDENCIES HoroEditorViewportScene HoroEditorViewportResources HoroRenderMetal HoroRenderFrontend)
horo_allow_target_dependencies(TARGET HoroGui
    DEPENDENCIES HoroEditorServices HoroFoundation HoroEditorRenderExtraction HoroExtensions)
horo_allow_target_dependencies(TARGET HoroExtensions
    DEPENDENCIES HoroFoundation HoroPlatform HoroAssets)

# Executables are composition roots and may select any production module.
horo_allow_target_dependencies(TARGET HoroHostModuleComposition DEPENDENCIES HoroFoundation)
horo_allow_target_dependencies(TARGET horo-engine DEPENDENCIES HoroApplication HoroHostModuleComposition)
horo_allow_target_dependencies(TARGET HoroEditor
    DEPENDENCIES
        HoroGui
        HoroEditorServices
        HoroEditorRenderExtraction
        HoroRenderFrontend
        HoroRuntime
        HoroRuntimeScene
        HoroExtensions
        HoroPlatform
        HoroProjectMigrations
        HoroInputSdl
        HoroOpenTelemetry
        HoroEditorViewportOpenGL
        HoroEditorViewportMetal
        HoroHostModuleComposition)

# These edges predate the target-level architecture. Each exception is tied to
# an active roadmap owner and must disappear when that ticket resolves the
# corresponding boundary.
horo_allow_temporary_dependency_exception(
    TARGET HoroSceneModel
    DEPENDENCY HoroRenderApi
    OWNER "Rendering"
    REMOVAL_TICKET "#275"
    REASON "Primitive mesh contracts still reuse renderer mesh types")
horo_allow_temporary_dependency_exception(
    TARGET HoroEditorServices
    DEPENDENCY HoroGameplayLua
    OWNER "Gameplay"
    REMOVAL_TICKET "#61"
    REASON "Editor services still select the concrete Lua gameplay adapter")
horo_allow_temporary_dependency_exception(
    TARGET HoroRenderNull
    DEPENDENCY HoroRenderBackendRegistry
    OWNER "Rendering"
    REMOVAL_TICKET "#62"
    REASON "Static backend registration predates the renderer module host")
horo_allow_temporary_dependency_exception(
    TARGET HoroRenderOpenGL
    DEPENDENCY HoroRenderBackendRegistry
    OWNER "Rendering"
    REMOVAL_TICKET "#62"
    REASON "Static backend registration predates the renderer module host")
horo_allow_temporary_dependency_exception(
    TARGET HoroRenderMetal
    DEPENDENCY HoroRenderBackendRegistry
    OWNER "Rendering"
    REMOVAL_TICKET "#62"
    REASON "Static backend registration predates the renderer module host")
