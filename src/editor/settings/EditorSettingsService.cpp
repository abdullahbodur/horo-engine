#include "Horo/Editor/EditorSettingsService.h"

#include "Horo/Editor/EditorConfiguration.h"
#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorSettingsEvents.h"
#include "Horo/Editor/Localization/LocalizationService.h"
#include "Horo/Foundation/Configuration.h"
#include "editor/EditorServiceErrors.h"

#include <string>
#include <utility>

namespace Horo::Editor
{
    namespace
    {
        [[nodiscard]] Error SettingsError(const ErrorCodeDescriptor &descriptor, std::string message)
        {
            return MakeError(descriptor, std::move(message));
        }
    } // namespace

    /** @copydoc EditorSettingsService::EditorSettingsService */
    EditorSettingsService::EditorSettingsService(EditorSettings initialSettings, ConfigurationService& configuration,
                                                 EditorDataBus& events, LocalizationService& localization)
        : m_committed(std::move(initialSettings)), m_configuration(configuration), m_events(events),
          m_localization(localization)
    {
        (void)ValidateEditorSettings(m_committed, nullptr);
    }

    /** @copydoc EditorSettingsService::Snapshot */
    EditorSettingsSnapshot EditorSettingsService::Snapshot() const noexcept
    {
        return EditorSettingsSnapshot{.settings = m_committed, .revision = m_revision};
    }

    /** @copydoc EditorSettingsService::Commit */
    Result<EditorSettingsSnapshot> EditorSettingsService::Commit(const EditorSettingsDraft& draft)
    {
        if (draft.baseRevision != m_revision)
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError(SettingsErrors::DraftStale, "Editor settings draft is stale."));
        }

        EditorSettings candidate = draft.settings;
        if (std::string validationError; !ValidateEditorSettings(candidate, &validationError))
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError(SettingsErrors::ValidationFailed, std::move(validationError)));
        }

        const bool languageChanged = candidate.languageTag != m_committed.languageTag;
        if (languageChanged)
        {
            const auto locale = LocaleTag::Parse(candidate.languageTag);
            LocalizationError localizationError;
            if (!locale.has_value() || !m_localization.Prepare(*locale, &localizationError))
            {
                return Result<EditorSettingsSnapshot>::Failure(
                    SettingsError(SettingsErrors::ValidationFailed, std::move(localizationError.message)));
            }
        }

        const ConfigurationSnapshot configurationSnapshot = m_configuration.Snapshot();
        const ConfigurationDraft appearanceDraft = MakeEditorAppearanceConfigurationDraft(
            configurationSnapshot, candidate);
        if (const Result<void> result = m_configuration.Validate(appearanceDraft); result.HasError())
        {
            return Result<EditorSettingsSnapshot>::Failure(result.ErrorValue());
        }

        EditorSettingsDocument document{.settings = candidate};
        if (std::string persistenceError; !SaveEditorSettingsDocument(&document, &persistenceError))
        {
            return Result<EditorSettingsSnapshot>::Failure(
                SettingsError(SettingsErrors::PersistenceFailed, std::move(persistenceError)));
        }

        if (languageChanged)
        {
            LocalizationError localizationError;
            if (!m_localization.ActivatePrepared(&localizationError))
            {
                return Result<EditorSettingsSnapshot>::Failure(
                    SettingsError(SettingsErrors::ValidationFailed, std::move(localizationError.message)));
            }
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
