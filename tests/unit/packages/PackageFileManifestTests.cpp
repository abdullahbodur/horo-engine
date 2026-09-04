#include "Horo/Packages/PackageFileManifest.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <nlohmann/json.hpp>

namespace {
    using Json = nlohmann::json;
    using Horo::Packages::PackageValidationLimits;
    using Horo::Packages::ValidatedPackageFileManifestV1;

    Json Entry(const std::string &path, const std::uint64_t size = 3) {
        return {{"path", path},
                {"size", size},
                {"sha256", Horo::FormatSha256(Horo::ComputeSha256({}))},
                {"executable", false},
                {"contributionRoot", nullptr}};
    }

    Json Manifest(Json entries) {
        return {{"schemaVersion", 1}, {"files", std::move(entries)}};
    }

    TEST_CASE("Package inventory retains typed fields and exact input digest", "[packages][manifest]") {
        auto file = Entry("assets/şölen.bin");
        file["contributionRoot"] = "assets";
        file["executable"] = true;
        const std::string text = Manifest(Json::array({file, Entry("NOTICE.md", 0)})).dump();
        const auto result = ValidatedPackageFileManifestV1::Parse(text);
        REQUIRE(result.HasValue());
        REQUIRE(result.Value().Entries().size() == 2);
        const auto &first = result.Value().Entries()[0];
        CHECK(first.id.path.Value() == "assets/şölen.bin");
        CHECK(first.size == 3);
        CHECK(first.executable);
        REQUIRE(first.contributionRoot);
        CHECK(first.contributionRoot->Value() == "assets");
        CHECK(first.digest == Horo::ComputeSha256({}));
        CHECK_FALSE(result.Value().Entries()[1].contributionRoot);
        CHECK(result.Value().Digest() == Horo::ComputeSha256(std::as_bytes(std::span{text})));
    }

    TEST_CASE("Package inventory rejects malformed roots and duplicate keys", "[packages][manifest]") {
        const std::array<std::string, 10> invalid{"",
                                                  "null",
                                                  "[]",
                                                  "{",
                                                  R"({"schemaVersion":1,"schemaVersion":1,"files":[]})",
                                                  R"({"schemaVersion":1,"files":[],"unknown":true})",
                                                  R"({"schemaVersion":1,"files":{}})",
                                                  R"({"schemaVersion":1.0,"files":[]})",
                                                  R"({"schemaVersion":2,"files":[]})",
                                                  R"({"files":[]})"};
        for (const auto &text : invalid) {
            CAPTURE(text);
            CHECK(ValidatedPackageFileManifestV1::Parse(text).HasError());
        }
        const auto entry = Entry("a").dump();
        auto duplicate = entry;
        duplicate.insert(1, R"("path":"a",)");
        CHECK(ValidatedPackageFileManifestV1::Parse("{\"schemaVersion\":1,\"files\":[" + duplicate + "]}").HasError());
        CHECK(ValidatedPackageFileManifestV1::Parse(R"({"schemaVersion":1,"files":[[[[[[[[[[]]]]]]]]]]})").HasError());
    }

    TEST_CASE("Package inventory enforces exact entry schema", "[packages][manifest]") {
        using Mutate = void (*)(Json &);
        const std::array<Mutate, 12> mutations{
            +[](Json &file) {
            file.erase("path");
        },
            +[](Json &file) {
            file["unknown"] = 1;
        },
            +[](Json &file) {
            file["path"] = 12;
        },
            +[](Json &file) {
            file["path"] = "../outside";
        },
            +[](Json &file) {
            file["size"] = -1;
        },
            +[](Json &file) {
            file["size"] = 3.0;
        },
            +[](Json &file) {
            file["sha256"] = "broken";
        },
            +[](Json &file) {
            file["executable"] = 1;
        },
            +[](Json &file) {
            file["contributionRoot"] = "other";
        },
            +[](Json &file) {
            file["contributionRoot"] = "../";
        },
            +[](Json &file) {
            file["contributionRoot"] = false;
        },
            +[](Json &file) {
            file = nullptr;
        },
        };
        for (const auto mutate : mutations) {
            auto file = Entry("assets/a");
            mutate(file);
            CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({file})).dump()).HasError());
        }
    }

    TEST_CASE("Package inventory rejects duplicate files and directory aliases in either order", "[packages][manifest]") {
        const std::array<std::pair<std::string, std::string>, 6> conflicts{std::pair{"a", "a"},      {"a", "A"},
                                                                           {"assets/a", "ASSETS/b"}, {"assets/a", "assets"},
                                                                           {"école/a", "ÉCOLE/b"},   {"Straße", "STRASSE"}};
        for (const auto &[first, second] : conflicts) {
            CAPTURE(first, second);
            CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({Entry(first), Entry(second)})).dump()).HasError());
            CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({Entry(second), Entry(first)})).dump()).HasError());
        }
        CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({Entry("a/b"), Entry("a/c")})).dump()).HasValue());
        CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({Entry("files.manifest.json")})).dump()).HasError());
        CHECK(ValidatedPackageFileManifestV1::Parse(Manifest(Json::array({Entry("Files.Manifest.Json")})).dump()).HasError());
    }

    TEST_CASE("Package inventory checks byte and count ceilings without aggregate overflow", "[packages][manifest]") {
        const auto text = Manifest(Json::array({Entry("a", 3), Entry("b", 3)})).dump();
        const PackageValidationLimits exact{.manifestBytes = text.size(), .fileBytes = 3, .expandedBytes = 6, .entries = 2};
        CHECK(ValidatedPackageFileManifestV1::Parse(text, exact).HasValue());
        const std::array limits{PackageValidationLimits{.manifestBytes = text.size() - 1}, PackageValidationLimits{.fileBytes = 2},
                                PackageValidationLimits{.expandedBytes = 5}, PackageValidationLimits{.entries = 1}};
        for (const auto &limit : limits) {
            CHECK(ValidatedPackageFileManifestV1::Parse(text, limit).HasError());
        }
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        const auto oversized = Manifest(Json::array({Entry("a", maximum), Entry("b", 1)})).dump();
        CHECK(ValidatedPackageFileManifestV1::Parse(oversized, {.fileBytes = maximum, .expandedBytes = maximum}).HasError());
    }
}  // namespace
