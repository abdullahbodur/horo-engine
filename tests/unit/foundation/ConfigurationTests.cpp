#include "Horo/Foundation/Configuration.h"
#include "Horo/Foundation/DataBus.h"

#include <cassert>
#include <string>

namespace
{

const Horo::SettingDescriptor kThemeDescriptor{
    .key = Horo::SettingKey{"editor.theme.active"},
    .type = Horo::SettingValueType::String,
    .defaultValue = std::string{"midnight"},
    .scope = Horo::SettingScope::User,
    .reloadPolicy = Horo::ReloadPolicy::NextFrame,
    .sensitivity = Horo::SettingSensitivity::Public,
};

const Horo::SettingDescriptor kAutosaveDescriptor{
    .key = Horo::SettingKey{"editor.autosave.enabled"},
    .type = Horo::SettingValueType::Boolean,
    .defaultValue = true,
    .scope = Horo::SettingScope::User,
    .reloadPolicy = Horo::ReloadPolicy::Immediate,
    .sensitivity = Horo::SettingSensitivity::Public,
};

Horo::ConfigurationService BuildService()
{
    Horo::ConfigurationSchema schema;
    assert(schema.Register(kThemeDescriptor).HasValue());
    assert(schema.Register(kAutosaveDescriptor).HasValue());
    assert(schema.Seal().HasValue());
    return Horo::ConfigurationService(std::move(schema));
}

void SchemaRejectsDuplicateKeys()
{
    Horo::ConfigurationSchema schema;
    assert(schema.Register(kThemeDescriptor).HasValue());
    assert(schema.Register(kThemeDescriptor).HasError());
}

void SnapshotStartsWithSchemaDefaults()
{
    const Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot snapshot = service.Snapshot();
    assert(snapshot.Revision() == 0);
    assert(std::get<std::string>(snapshot.Get(Horo::SettingKey{"editor.theme.active"})) == "midnight");
    assert(std::get<bool>(snapshot.Get(Horo::SettingKey{"editor.autosave.enabled"})));
}

void CommitPublishesAWholeNewSnapshot()
{
    Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot before = service.Snapshot();

    Horo::ConfigurationDraft draft{.baseRevision = before.Revision()};
    draft.proposedValues.emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    draft.proposedValues.emplace(Horo::SettingKey{"editor.autosave.enabled"}, false);
    assert(service.Commit(draft).HasValue());

    const Horo::ConfigurationSnapshot after = service.Snapshot();
    assert(after.Revision() == 1);
    assert(std::get<std::string>(before.Get(Horo::SettingKey{"editor.theme.active"})) == "midnight");
    assert(std::get<std::string>(after.Get(Horo::SettingKey{"editor.theme.active"})) == "light");
    assert(!std::get<bool>(after.Get(Horo::SettingKey{"editor.autosave.enabled"})));
}

void CommitRejectsStaleDraft()
{
    Horo::ConfigurationService service = BuildService();
    Horo::ConfigurationDraft stale{.baseRevision = 8};
    stale.proposedValues.emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    const auto result = service.Commit(stale);
    assert(result.HasError());
    assert(result.ErrorValue().code.Value() == "configuration.draft_stale");
}

void ValidationDoesNotMutateSnapshot()
{
    Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot before = service.Snapshot();
    Horo::ConfigurationDraft draft{.baseRevision = before.Revision()};
    draft.proposedValues.emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});

    assert(service.Validate(draft).HasValue());
    assert(service.Snapshot().Revision() == before.Revision());
}

void CommitPublishesOnlyBoundedChangeMetadata()
{
    Horo::EngineDataBus events{Horo::EngineDataBusConfig{.traceDispatch = false}};
    Horo::ConfigurationSchema schema;
    assert(schema.Register(kThemeDescriptor).HasValue());
    assert(schema.Seal().HasValue());
    Horo::ConfigurationService service{std::move(schema), &events};

    Horo::ConfigurationChangedEvent observed{};
    const Horo::Subscription subscription = events.Subscribe<Horo::ConfigurationChangedEvent>([&observed](const auto &event) { observed = event; });
    Horo::ConfigurationDraft draft{.baseRevision = 0};
    draft.proposedValues.emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    assert(service.Commit(draft).HasValue());

    assert(observed.revision == 1);
    assert(observed.changedKeys.size() == 1);
    assert(observed.changedKeys.front().Value() == "editor.theme.active");
}

void JsonSerializationAndDeserialization()
{
    Horo::ConfigurationService service = BuildService();
    constexpr std::string json = R"json({
  "revision": 0,
  "values": {
    "editor.theme.active": "solarized",
    "editor.autosave.enabled": false
  }
})json";
    assert(service.LoadJson(json).HasValue());
    const auto snapshot = service.Snapshot();
    assert(snapshot.Revision() == 1);
    assert(std::get<std::string>(snapshot.Get(Horo::SettingKey{"editor.theme.active"})) == "solarized");
    assert(!std::get<bool>(snapshot.Get(Horo::SettingKey{"editor.autosave.enabled"})));

    const std::string exported = snapshot.ToJson();
    assert(exported.find("\"editor.theme.active\": \"solarized\"") != std::string::npos);
    assert(exported.find("\"editor.autosave.enabled\": false") != std::string::npos);
}

void FilePersistence()
{
    Horo::ConfigurationService service = BuildService();
    constexpr std::string testPath = "test_config_persistence.json";
    Horo::ConfigurationDraft draft{.baseRevision = 0};
    draft.proposedValues.emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"dark_pro"});
    assert(service.Commit(draft).HasValue());
    assert(service.SaveFile(testPath).HasValue());

    Horo::ConfigurationService loader = BuildService();
    assert(loader.LoadFile(testPath).HasValue());
    assert(std::get<std::string>(loader.Snapshot().Get(Horo::SettingKey{"editor.theme.active"})) == "dark_pro");
    std::remove(testPath.c_str());
}

} // namespace

int main()
{
    SchemaRejectsDuplicateKeys();
    SnapshotStartsWithSchemaDefaults();
    CommitPublishesAWholeNewSnapshot();
    CommitRejectsStaleDraft();
    ValidationDoesNotMutateSnapshot();
    CommitPublishesOnlyBoundedChangeMetadata();
    JsonSerializationAndDeserialization();
    FilePersistence();
    return 0;
}
