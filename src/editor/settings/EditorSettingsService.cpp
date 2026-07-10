#include "Horo/Editor/EditorSettingsService.h"

#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Foundation/Configuration.h"

#include <string>
#include <utility>

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] Error SettingsError(const char *code, std::string message)
        {
            return Error{ErrorCode{code}, ErrorDomainId{"horo.editor.settings"}, ErrorSeverity::Error, std::move(message)};
        }
    } // namespace

    /** @copydoc EditorSettingsService::EditorSettingsService */
    EditorSettingsService::EditorSettingsService(EditorSettings initialSettings, ConfigurationService &configuration,
                                                 EditorDataBus &events)
        : m_committed(std::move(initialSettings)), m_configuration(configuration), m_events(events)
    {
        (void)ValidateEditorSettings(m_committed, nullptr);
    }

    /** @copydoc EditorSettingsService::Snapshot */
    EditorSettingsSnapshot EditorSettingsService::Snapshot() const noexcept
    {
        return EditorSettingsSnapshot{.settings = m_committed, .revision = m_revision};
    }

    /** @copydoc EditorSettingsService::Commit */
    Result<EditorSettingsSnapshot> EditorSettingsService::Commit(const EditorSettingsDraft &draft)
    {
        if (draft.baseRevision != m_revision)
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError("editor.settings.draft_stale", "Editor settings draft is stale."));
        }

        EditorSettings candidate = draft.settings;
        std::string validationError;
        if (!ValidateEditorSettings(candidate, &validationError))
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError("editor.settings.validation_failed", std::move(validationError)));
        }

        const ConfigurationSnapshot configurationSnapshot = m_configuration.Snapshot();
        const ConfigurationDraft appearanceDraft =
            MakeEditorAppearanceConfigurationDraft(configurationSnapshot, candidate);
        if (const Result<void> result = m_configuration.Validate(appearanceDraft); result.HasError())
        {
            return Result<EditorSettingsSnapshot>::Failure(result.ErrorValue());
        }

        EditorSettingsDocument document{.settings = candidate};
        std::string persistenceError;
        if (!SaveEditorSettingsDocument(&document, &persistenceError))
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError("editor.settings.persistence_failed", std::move(persistenceError)));
        }

        if (const Result<void> result = m_configuration.Commit(appearanceDraft); result.HasError())
        {
            return Result<EditorSettingsSnapshot>::Failure(result.ErrorValue());
        }

        m_committed = std::move(document.settings);
        ++m_revision;
        const EditorSettingsSnapshot activated = Snapshot();
        m_events.Publish(EditorSettingsChangedEvent{
            .revision = activated.revision,
            .phase = SettingsChangePhase::Committed,
            .changedDomains = SettingsDomain::All,
        });
        return Result<EditorSettingsSnapshot>::Success(activated);
    }
} // namespace Horo::Editor
