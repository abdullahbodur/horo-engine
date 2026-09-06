include_guard(GLOBAL)

# Public header ownership is intentionally explicit. Adding a header under
# include/Horo requires assigning it to exactly one production target here.
message(STATUS "Configuring target-specific public header boundaries")

horo_configure_target_header_boundary(HoroFoundation PUBLIC_HEADERS
    Horo/Foundation/BuildOutputStore.h
    Horo/Foundation/CancellationToken.h
    Horo/Foundation/Configuration.h
    Horo/Foundation/DataBus.h
    Horo/Foundation/Diagnostics.h
    Horo/Foundation/Diagnostics/DiagnosticBundle.h
    Horo/Foundation/Diagnostics/OperationHistory.h
    Horo/Foundation/DiagnosticsEngine.h
    Horo/Foundation/ErrorCode.h
    Horo/Foundation/Handles.h
    Horo/Foundation/JobSystem.h
    Horo/Foundation/Logging/LogContext.h
    Horo/Foundation/Logging/LogLevel.h
    Horo/Foundation/Logging/Logger.h
    Horo/Foundation/Logging/StructuredLogStore.h
    Horo/Foundation/ModuleDescriptor.h
    Horo/Foundation/ModuleHost.h
    Horo/Foundation/OperationStore.h
    Horo/Foundation/PathUtils.h
    Horo/Foundation/Paths.h
    Horo/Foundation/Platform.h
    Horo/Foundation/Progress.h
    Horo/Foundation/Result.h
    Horo/Foundation/Sha256.h
    Horo/Foundation/StrongId.h
    Horo/Foundation/String.h
    Horo/Foundation/Telemetry/Operation.h
    Horo/Foundation/Telemetry/Telemetry.h
    Horo/Foundation/Time.h
    Horo/Foundation/TransparentString.h
    Horo/Math/SceneMath.h
    Horo/Math/WorldCoordinate64.h
)

horo_configure_target_header_boundary(HoroOpenTelemetry PUBLIC_HEADERS
    Horo/Foundation/Telemetry/OpenTelemetrySink.h)

horo_configure_target_header_boundary(HoroPlatform PUBLIC_HEADERS
    Horo/Platform/DynamicLibrary.h
    Horo/Platform/ExternalProcess.h
    Horo/Platform/PlatformErrors.h
)

horo_configure_target_header_boundary(HoroApplication PUBLIC_HEADERS
    Horo/Application/HostObservability.h
    Horo/Application/ProjectCompatibility.h
    Horo/Application/ProjectMigration.h
    Horo/Application/ProjectMigrationCatalog.h
    Horo/Application/ProjectVersion.h
)
horo_configure_target_header_boundary(HoroPackages PUBLIC_HEADERS
    Horo/Packages/PackagePath.h
    Horo/Packages/PackageArchive.h
    Horo/Packages/PackageFileManifest.h
)
horo_configure_target_header_boundary(HoroProjectMigrations)
horo_configure_target_header_boundary(HoroRuntime PUBLIC_HEADERS
    Horo/Runtime/FrameScheduler.h
    Horo/Runtime/RuntimeHost.h
    Horo/Runtime/RuntimeLifecycle.h
    Horo/Runtime/Save/SaveErrors.h
    Horo/Runtime/Save/SaveIdentity.h
    Horo/Runtime/Save/SaveParticipantRegistry.h
    Horo/Runtime/Save/SaveArchiveMetadata.h
    Horo/Runtime/Save/SaveArchiveFraming.h
)
horo_configure_target_header_boundary(HoroRuntimeUi PUBLIC_HEADERS
    Horo/Runtime/Ui/UiErrors.h
    Horo/Runtime/Ui/UiIdentity.h
    Horo/Runtime/Ui/UiDocument.h
)
horo_configure_target_header_boundary(HoroNetworkApi PUBLIC_HEADERS
    Horo/Network/MessageCodecRegistry.h
    Horo/Network/MessageEnvelope.h
    Horo/Network/NetworkErrors.h
    Horo/Network/ProtocolIdentity.h
    Horo/Network/ProtocolIdentityRegistry.h
    Horo/Network/ReplicationDescriptor.h
    Horo/Network/ReplicationDescriptorRegistry.h
    Horo/Network/ReplicationIdentity.h
    Horo/Network/TransportCapabilities.h
)

horo_configure_target_header_boundary(HoroGameplayApi PUBLIC_HEADERS
    Horo/Gameplay/Behavior.h
    Horo/Gameplay/BehaviorTypes.h
    Horo/Gameplay/Component.h
    Horo/Gameplay/ComponentRegistry.h
    Horo/Gameplay/GameServiceRegistry.h
    Horo/Gameplay/GameModule.h
    Horo/Gameplay/GameplayErrors.h
    Horo/Gameplay/GameplayRegistration.h
    Horo/Gameplay/NativeBehavior.h
    Horo/Gameplay/SystemRegistry.h
)
horo_configure_target_header_boundary(HoroRuntimeScene PUBLIC_HEADERS
    Horo/Runtime/Scene/RuntimeScene.h
    Horo/Runtime/Scene/RuntimeSceneDefinition.h
)
horo_configure_target_header_boundary(HoroGameplayRuntime PUBLIC_HEADERS
    Horo/Gameplay/BehaviorRegistry.h
    Horo/Gameplay/BehaviorRuntime.h
    Horo/Gameplay/GameplayRegistrationRuntime.h
)
horo_configure_target_header_boundary(HoroGameplayModuleHost PUBLIC_HEADERS
    Horo/Gameplay/GameModuleHost.h
)
horo_configure_target_header_boundary(HoroGameplayBuild PUBLIC_HEADERS
    Horo/Application/GameplayBuildService.h
)
horo_configure_target_header_boundary(HoroGameplayLua PUBLIC_HEADERS
    Horo/Gameplay/LuaBehavior.h
)

horo_configure_target_header_boundary(HoroAssets PUBLIC_HEADERS
    Horo/Assets/AssetCook.h
    Horo/Assets/AssetCookCache.h
    Horo/Assets/AssetCookOutput.h
    Horo/Assets/AssetCookService.h
    Horo/Assets/AssetId.h
    Horo/Assets/AssetImportMetadata.h
    Horo/Assets/AssetImportOperation.h
    Horo/Assets/AssetImporter.h
    Horo/Assets/AssetPreview.h
    Horo/Assets/AssetProvider.h
    Horo/Assets/AssetRegistry.h
    Horo/Assets/AssetReimport.h
    Horo/Assets/CookCatalog.h
    Horo/Assets/MeshEditorPayload.h
)
horo_configure_target_header_boundary(HoroAudioApi PUBLIC_HEADERS
    Horo/Audio/AudioBackendCapabilities.h
    Horo/Audio/AudioCallbackEvents.h
    Horo/Audio/AudioDeviceDiscovery.h
    Horo/Audio/AudioDeviceNegotiation.h
    Horo/Audio/AudioDeviceTiming.h
    Horo/Audio/AudioFormat.h
    Horo/Audio/AudioPlanarBlock.h
    Horo/Audio/AudioErrors.h
    Horo/Audio/AudioIdentity.h
    Horo/Audio/AudioResamplerPlan.h
)
horo_configure_target_header_boundary(HoroAudioDsp PUBLIC_HEADERS
    Horo/Audio/AudioResampler.h
)
horo_configure_target_header_boundary(HoroAudioMemory PUBLIC_HEADERS
    Horo/Audio/AudioMemory.h
)
horo_configure_target_header_boundary(HoroAudioCommands PUBLIC_HEADERS
    Horo/Audio/AudioCommands.h
    Horo/Audio/AudioCommandBuffer.h
    Horo/Audio/AudioCommandStaging.h
)
horo_configure_target_header_boundary(HoroInput PUBLIC_HEADERS
    Horo/Runtime/Input.h
)
horo_configure_target_header_boundary(HoroPhysics PUBLIC_HEADERS
    Horo/Physics/PhysicsBodyDescriptor.h
    Horo/Physics/PhysicsCapabilities.h
    Horo/Physics/PhysicsConstraintDescriptor.h
    Horo/Physics/PhysicsCookedShapeDescriptor.h
    Horo/Physics/PhysicsErrors.h
    Horo/Physics/PhysicsFilterIdentity.h
    Horo/Physics/PhysicsIdentity.h
    Horo/Physics/PhysicsPose.h
    Horo/Physics/PhysicsQuery.h
    Horo/Physics/PhysicsShapeDescriptor.h
    Horo/Physics/PhysicsStepPolicy.h
    Horo/Physics/PhysicsTickPipeline.h
    Horo/Physics/PhysicsWorld.h
    Horo/Physics/PhysicsWorldBudgets.h
    Horo/Physics/PhysicsWorldDescriptor.h
    Horo/Physics/PhysicsWorldSettings.h
)
horo_configure_target_header_boundary(HoroAI PUBLIC_HEADERS
    Horo/AI/AIErrors.h
    Horo/AI/AIIdentity.h
)
horo_configure_target_header_boundary(HoroNavigationApi PUBLIC_HEADERS
    Horo/Navigation/NavigationAreas.h
    Horo/Navigation/NavigationAgentProfiles.h
    Horo/Navigation/NavigationBackend.h
    Horo/Navigation/NavigationCapabilities.h
    Horo/Navigation/NavigationErrors.h
    Horo/Navigation/NavigationIdentity.h
    Horo/Navigation/NavigationOutcomes.h
)
horo_configure_target_header_boundary(HoroNavigationNull PUBLIC_HEADERS
    Horo/Navigation/Backends/NullProvider.h
)
horo_configure_target_header_boundary(HoroWorldStreaming PUBLIC_HEADERS
    Horo/WorldStreaming/StreamingSourceDescriptor.h
    Horo/WorldStreaming/WorldCellQuantization.h
    Horo/WorldStreaming/WorldStreamingErrors.h
    Horo/WorldStreaming/WorldStreamingIdentity.h
)

horo_configure_target_header_boundary(HoroPrefab PUBLIC_HEADERS
    Horo/Prefab/PrefabErrors.h
    Horo/Prefab/PrefabIdentity.h
    Horo/Prefab/PrefabLimits.h
)

horo_configure_target_header_boundary(HoroPrefabAuthoring PUBLIC_HEADERS
    Horo/Prefab/PrefabDocument.h
)

horo_configure_target_header_boundary(HoroRenderApi PUBLIC_HEADERS
    Horo/Runtime/Render/Mesh.h
    Horo/Runtime/Render/RenderAdapter.h
    Horo/Runtime/Render/RenderAdapterErrors.h
    Horo/Runtime/Render/RenderBackend.h
    Horo/Runtime/Render/RenderGraph.h
    Horo/Runtime/Render/RenderGraphErrors.h
    Horo/Runtime/Render/RenderResourceDescriptorErrors.h
    Horo/Runtime/Render/RenderResourceDescriptors.h
    Horo/Runtime/Render/RenderSubmission.h
    Horo/Runtime/Render/Texture.h
    Horo/Runtime/Render/RenderResource.h
    Horo/Runtime/Render/RenderScene.h
)
horo_configure_target_header_boundary(HoroRenderBackendRegistry PUBLIC_HEADERS
    Horo/Runtime/Render/RenderBackendRegistry.h
)
horo_configure_target_header_boundary(HoroRenderFrontend PUBLIC_HEADERS
    Horo/Runtime/Render/RenderFrontend.h
)
horo_configure_target_header_boundary(HoroSceneModel PUBLIC_HEADERS
    Horo/Runtime/Scene/PrimitiveCatalog.h
    Horo/Runtime/Scene/PrimitiveMesh.h
    Horo/Runtime/Scene/PrimitiveMeshDescriptor.h
    Horo/Runtime/Scene/SceneComponents.h
)
horo_configure_target_header_boundary(HoroRenderNull PUBLIC_HEADERS
    Horo/Runtime/Render/NullBackendModule.h
)
horo_configure_target_header_boundary(HoroRenderOpenGL)
horo_configure_target_header_boundary(HoroRenderMetal)

horo_configure_target_header_boundary(HoroEditorModel PUBLIC_HEADERS
    Horo/Editor/EditorDataBus.h
)
horo_configure_target_header_boundary(HoroEditorViewportScene)
horo_configure_target_header_boundary(HoroEditorRenderExtraction)

horo_configure_target_header_boundary(HoroEditorServices PUBLIC_HEADERS
    Horo/Editor/ActivityBarLayout.h
    Horo/Editor/EditorConfiguration.h
    Horo/Editor/EditorMenuModel.h
    Horo/Editor/EditorModalHost.h
    Horo/Editor/EditorServiceRegistry.h
    Horo/Editor/EditorSettingsEvents.h
    Horo/Editor/EditorSettingsService.h
    Horo/Editor/EditorSettingsStore.h
    Horo/Editor/EditorStatusBarModel.h
    Horo/Editor/EditorWorkspaceEvents.h
    Horo/Editor/GuiRoute.h
    Horo/Editor/HierarchyModel.h
    Horo/Editor/Localization/ILocalizationService.h
    Horo/Editor/Localization/LocalizationService.h
    Horo/Editor/Localization/LocalizationTypes.h
    Horo/Editor/NotificationService.h
    Horo/Editor/ProjectCreationController.h
    Horo/Editor/ProjectCreationService.h
    Horo/Editor/ProjectIntegrityValidatorService.h
    Horo/Editor/ProjectMigrationTransaction.h
    Horo/Editor/ProjectMutation.h
    Horo/Editor/ProjectOpenService.h
    Horo/Editor/ProjectSession.h
    Horo/Editor/RecentProject.h
    Horo/Editor/RecentProjectInspectionService.h
    Horo/Editor/WelcomeController.h
    Horo/Editor/WorkspaceDockArea.h
    Horo/Editor/WorkspaceLayout.h
    Horo/Editor/WorkspaceLayoutPersistence.h
    Horo/Editor/WorkspacePanelHost.h
)

horo_configure_target_header_boundary(HoroEditorViewportOpenGL)
horo_configure_target_header_boundary(HoroEditorViewportMetal)
horo_configure_target_header_boundary(HoroInputSdl)
horo_configure_target_header_boundary(HoroGui PUBLIC_HEADERS
    Horo/Editor/AssetImportModal.h
    Horo/Editor/DefaultScreenFactories.h
    Horo/Editor/DefaultWorkspacePanels.h
    Horo/Editor/DesignSystem/DesignTokens.h
    Horo/Editor/EditorGuiContext.h
    Horo/Editor/EditorIcons.h
    Horo/Editor/EditorSnackbarHost.h
    Horo/Editor/EditorTheme.h
    Horo/Editor/EditorUiComponents.h
    Horo/Editor/GuiScreen.h
    Horo/Editor/GuiScreenHost.h
    Horo/Editor/IWorkspacePanel.h
    Horo/Editor/ScreenRegistry.h
    Horo/Editor/SettingsModal.h
    Horo/Editor/SettingsModalDraft.h
    Horo/Editor/WorkspacePanelRegistry.h
)

horo_configure_target_header_boundary(HoroExtensions PUBLIC_HEADERS
    Horo/Extensions/ExtensionAbi.h
    Horo/Extensions/ExtensionDiscovery.h
    Horo/Extensions/ExtensionErrors.h
    Horo/Extensions/ExtensionInventory.h
    Horo/Extensions/ExtensionManager.h
    Horo/Extensions/ExtensionManifest.h
    Horo/Extensions/ExtensionMarketplace.h
)

horo_verify_public_header_inventory()
message(STATUS "Target-specific public header inventory is complete")
