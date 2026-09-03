#include "DiscoveryPaths.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace Horo::Extensions::Discovery::Tests {
    namespace fs = std::filesystem;

    struct DiscoveryPathFixture {
        fs::path directory =
            fs::temp_directory_path() / ("horo-discovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::path root = directory / "approved space";

        DiscoveryPathFixture() {
            REQUIRE(fs::create_directory(directory));
            fs::create_directories(root / "paket-ç" / "nested");
            fs::create_directories(directory / "approved space-other");
            std::ofstream(root / "paket-ç" / "extension.json") << "{}";
            root = fs::canonical(root);
        }

        ~DiscoveryPathFixture() {
            std::error_code error;
            fs::remove_all(directory, error);
        }
    };

    TEST_CASE_METHOD(DiscoveryPathFixture, "Discovery roots must be existing absolute directories", "[Extensions][Discovery]") {
        CHECK(CanonicalRoot(root).HasValue());
        CHECK(CanonicalRoot(root / "paket-ç" / ".").Value() == fs::canonical(root / "paket-ç"));
        CHECK(CanonicalRoot("relative").HasError());
        CHECK(CanonicalRoot({}).HasError());
        CHECK(CanonicalRoot(root / "missing").HasError());
        CHECK(CanonicalRoot(root / "paket-ç" / "extension.json").HasError());
    }

    TEST_CASE_METHOD(DiscoveryPathFixture, "Discovery only accepts existing strict descendants", "[Extensions][Discovery]") {
        const auto file = ResolveContainedPath(root, "paket-ç/extension.json");
        REQUIRE(file.HasValue());
        CHECK(file.Value() == root / "paket-ç" / "extension.json");
        CHECK(ResolveContainedPath(root, "paket-ç/nested").HasValue());
        CHECK(ResolveContainedPath(root, "paket-ç/./extension.json").HasValue());
        CHECK(ResolveContainedPath(root, "missing").HasError());
        CHECK(ResolveContainedPath(root, {}).HasError());
        CHECK(ResolveContainedPath(root, ".").HasError());
        CHECK(ResolveContainedPath(root, root / "paket-ç").HasError());
        CHECK(ResolveContainedPath(root, "../approved space-other").HasError());
        CHECK(ResolveContainedPath(root, "paket-ç/../paket-ç/extension.json").HasError());
    }

    TEST_CASE_METHOD(DiscoveryPathFixture, "Discovery resolves links without permitting escapes", "[Extensions][Discovery]") {
        std::error_code error;
        fs::create_directory_symlink(root / "paket-ç", root / "inside", error);
        if (error)
            SKIP("This host does not permit creation of directory symlinks: " << error.message());
        fs::create_directory_symlink(directory / "approved space-other", root / "outside");
        fs::create_directory_symlink(root, root / "self");
        fs::create_directory_symlink(root / "absent", root / "dangling");
        fs::create_symlink(directory / "approved space-other", root / "paket-ç" / "escaped.json");
        CHECK(ResolveContainedPath(root, "inside/extension.json").Value() == root / "paket-ç" / "extension.json");
        CHECK(ResolveContainedPath(root, "outside").HasError());
        CHECK(ResolveContainedPath(root, "self").HasError());
        CHECK(ResolveContainedPath(root, "dangling").HasError());
        CHECK(ResolveContainedPath(root, "paket-ç/escaped.json").HasError());
    }
}  // namespace Horo::Extensions::Discovery::Tests
