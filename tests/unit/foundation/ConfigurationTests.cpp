#include <catch2/catch_test_macros.hpp>

#include "Horo/Foundation/Configuration.h"
#include "Horo/Foundation/DataBus.h"

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
    REQUIRE((schema.Register(kThemeDescriptor).HasValue()));
    REQUIRE((schema.Register(kAutosaveDescriptor).HasValue()));
    REQUIRE((schema.Seal().HasValue()));
    return Horo::ConfigurationService(std::move(schema));
}

TEST_CASE("Schema Rejects Duplicate Keys", "[unit][foundation]")
{
    Horo::ConfigurationSchema schema;
    REQUIRE((schema.Register(kThemeDescriptor).HasValue()));
    REQUIRE((schema.Register(kThemeDescriptor).HasError()));
}

TEST_CASE("Snapshot Starts With Schema Defaults", "[unit][foundation]")
{
    const Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot snapshot = service.Snapshot();
    REQUIRE((snapshot.Revision() == 0));
    REQUIRE((std::get<std::string>(snapshot.Get(Horo::SettingKey{"editor.theme.active"})) == "midnight"));
    REQUIRE((std::get<bool>(snapshot.Get(Horo::SettingKey{"editor.autosave.enabled"}))));
}

TEST_CASE("Commit Publishes A Whole New Snapshot", "[unit][foundation]")
{
    Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot before = service.Snapshot();

    Horo::ConfigurationDraft draft{.baseRevision = before.Revision()};
    draft.proposedValues.try_emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    draft.proposedValues.try_emplace(Horo::SettingKey{"editor.autosave.enabled"}, false);
    REQUIRE((service.Commit(draft).HasValue()));

    const Horo::ConfigurationSnapshot after = service.Snapshot();
    REQUIRE((after.Revision() == 1));
    REQUIRE((std::get<std::string>(before.Get(Horo::SettingKey{"editor.theme.active"})) == "midnight"));
    REQUIRE((std::get<std::string>(after.Get(Horo::SettingKey{"editor.theme.active"})) == "light"));
    REQUIRE((!std::get<bool>(after.Get(Horo::SettingKey{"editor.autosave.enabled"}))));
}

TEST_CASE("Commit Rejects Stale Draft", "[unit][foundation]")
{
    Horo::ConfigurationService service = BuildService();
    Horo::ConfigurationDraft stale{.baseRevision = 8};
    stale.proposedValues.try_emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    const auto result = service.Commit(stale);
    REQUIRE((result.HasError()));
    REQUIRE((result.ErrorValue().code.Value() == "configuration.draft_stale"));
}

TEST_CASE("Validation Does Not Mutate Snapshot", "[unit][foundation]")
{
    Horo::ConfigurationService service = BuildService();
    const Horo::ConfigurationSnapshot before = service.Snapshot();
    Horo::ConfigurationDraft draft{.baseRevision = before.Revision()};
    draft.proposedValues.try_emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});

    REQUIRE((service.Validate(draft).HasValue()));
    REQUIRE((service.Snapshot().Revision() == before.Revision()));
}

TEST_CASE("Commit Publishes Only Bounded Change Metadata", "[unit][foundation]")
{
    Horo::EngineDataBus events{Horo::EngineDataBusConfig{.traceDispatch = false}};
    Horo::ConfigurationSchema schema;
    REQUIRE((schema.Register(kThemeDescriptor).HasValue()));
    REQUIRE((schema.Seal().HasValue()));
    Horo::ConfigurationService service{std::move(schema), &events};

    Horo::ConfigurationChangedEvent observed{};
    const Horo::Subscription subscription =
        events.Subscribe<Horo::ConfigurationChangedEvent>([&observed](const auto &event) { observed = event; });
    Horo::ConfigurationDraft draft{.baseRevision = 0};
    draft.proposedValues.try_emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"light"});
    REQUIRE((service.Commit(draft).HasValue()));

    REQUIRE((observed.revision == 1));
    REQUIRE((observed.changedKeys.size() == 1));
    REQUIRE((observed.changedKeys.front().Value() == "editor.theme.active"));
}

TEST_CASE("Json Serialization And Deserialization", "[unit][foundation]")
{
    Horo::ConfigurationService service = BuildService();
    const std::string json = R"json({
  "revision": 0,
  "values": {
    "editor.theme.active": "solarized",
    "editor.autosave.enabled": false
  }
})json";
    REQUIRE((service.LoadJson(json).HasValue()));
    const auto snapshot = service.Snapshot();
    REQUIRE((snapshot.Revision() == 1));
    REQUIRE((std::get<std::string>(snapshot.Get(Horo::SettingKey{"editor.theme.active"})) == "solarized"));
    REQUIRE((!std::get<bool>(snapshot.Get(Horo::SettingKey{"editor.autosave.enabled"}))));

    const std::string exported = snapshot.ToJson();
    REQUIRE((exported.find("\"editor.theme.active\": \"solarized\"") != std::string::npos));
    REQUIRE((exported.find("\"editor.autosave.enabled\": false") != std::string::npos));
}

TEST_CASE("File Persistence", "[unit][foundation]")
{
    Horo::ConfigurationService service = BuildService();
    const std::string testPath = "test_config_persistence.json";
    Horo::ConfigurationDraft draft{.baseRevision = 0};
    draft.proposedValues.try_emplace(Horo::SettingKey{"editor.theme.active"}, std::string{"dark_pro"});
    REQUIRE((service.Commit(draft).HasValue()));
    REQUIRE((service.SaveFile(testPath).HasValue()));

    Horo::ConfigurationService loader = BuildService();
    REQUIRE((loader.LoadFile(testPath).HasValue()));
    REQUIRE((std::get<std::string>(loader.Snapshot().Get(Horo::SettingKey{"editor.theme.active"})) == "dark_pro"));
    std::remove(testPath.c_str());
}
} // namespace
