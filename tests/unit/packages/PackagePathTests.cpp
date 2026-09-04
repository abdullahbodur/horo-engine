#include "Horo/Packages/PackagePath.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>

using Horo::Packages::PackagePath;

TEST_CASE("Package file identities preserve portable Unicode spelling", "[packages][path]") {
    constexpr std::array paths{"horo-package.toml", "assets/my model.bin", "assets/şölen.bin", "资料/模型.bin", "assets/.hidden"};
    for (const std::string_view path : paths) {
        const auto parsed = PackagePath::Parse(path);
        REQUIRE(parsed.HasValue());
        CHECK(parsed.Value().Value() == path);
    }
}

TEST_CASE("Package paths reject traversal and platform aliases", "[packages][path]") {
    constexpr std::array invalid{"",
                                 "/file",
                                 "../file",
                                 "a/../b",
                                 "a/./b",
                                 "a//b",
                                 "a/",
                                 "C:/file",
                                 "C:file",
                                 "\\\\host\\file",
                                 "a\\b",
                                 "file:stream",
                                 "name.",
                                 "name /file",
                                 "NUL.txt",
                                 "Con",
                                 "aux",
                                 "PRN",
                                 "COM1.txt",
                                 "lPt9",
                                 "com¹.txt",
                                 "conin$",
                                 "bad?file",
                                 "bad*file",
                                 "bad|file",
                                 "bad<file",
                                 "bad>file",
                                 "bad\"file",
                                 "e\xcc\x81.txt",
                                 "\xff",
                                 "a\nfile",
                                 "a\tfile",
                                 "a\xe2\x80\xae.txt"};
    for (const std::string_view path : invalid) {
        CAPTURE(path);
        const auto parsed = PackagePath::Parse(path);
        REQUIRE(parsed.HasError());
        CHECK(parsed.ErrorValue().code.Value() == "packages.path.invalid");
    }
    CHECK(PackagePath::Parse(std::string_view{"a\0b", 3}).HasError());
    CHECK(PackagePath::Parse("CON .txt").HasError());
}

TEST_CASE("Package collision keys fold Unicode independently from host locale", "[packages][path]") {
    const auto upper = PackagePath::Parse("assets/ÉCOLE.bin");
    const auto lower = PackagePath::Parse("assets/école.bin");
    REQUIRE(upper.HasValue());
    REQUIRE(lower.HasValue());
    CHECK(upper.Value().CollisionKey() == lower.Value().CollisionKey());
    const auto sharpS = PackagePath::Parse("Straße.bin");
    const auto expanded = PackagePath::Parse("STRASSE.bin");
    REQUIRE(sharpS.HasValue());
    REQUIRE(expanded.HasValue());
    CHECK(sharpS.Value().CollisionKey() == expanded.Value().CollisionKey());
}

TEST_CASE("Package path limits include exact boundary values", "[packages][path]") {
    CHECK(PackagePath::Parse(std::string(255, 'a')).HasValue());
    CHECK(PackagePath::Parse(std::string(256, 'a')).HasError());
    std::string path = "a";
    for (int index = 1; index < 24; ++index) {
        path += "/a";
    }
    CHECK(PackagePath::Parse(path).HasValue());
    CHECK(PackagePath::Parse(path + "/a").HasError());
    path = std::string(255, 'a') + "/" + std::string(255, 'b') + "/" + std::string(255, 'c') + "/" + std::string(255, 'd');
    CHECK(PackagePath::Parse(path).HasValue());
    CHECK(PackagePath::Parse(path + "/a").HasError());
    path = std::string(250, 'a') + "/" + std::string(250, 'b') + "/" + std::string(250, 'c') + "/" + std::string(250, 'd') + "/" +
           std::string(20, 'e');
    REQUIRE(path.size() == 1024);
    CHECK(PackagePath::Parse(path).HasValue());
    CHECK(PackagePath::Parse(path + "e").HasError());
}

TEST_CASE("Package normalization preserves four-byte UTF-8 and expands compatibility ligatures", "[packages][path]") {
    const auto supplementary = PackagePath::Parse("assets/😀.bin");
    REQUIRE(supplementary.HasValue());
    CHECK(supplementary.Value().Value() == "assets/😀.bin");
    CHECK(supplementary.Value().CollisionKey() == "assets/😀.bin");
    const auto ligature = PackagePath::Parse("assets/ﬃ.bin");
    REQUIRE(ligature.HasValue());
    CHECK(ligature.Value().Value() == "assets/ﬃ.bin");
    CHECK(ligature.Value().CollisionKey() == "assets/ffi.bin");
    const auto composed = PackagePath::Parse("assets/각.bin");
    REQUIRE(composed.HasValue());
    CHECK(composed.Value().CollisionKey() == "assets/각.bin");
}
