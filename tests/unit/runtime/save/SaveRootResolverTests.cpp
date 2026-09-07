#include "Horo/Foundation/Platform.h"
#include "Horo/Runtime/Save/SaveErrors.h"
#include "Horo/Runtime/Save/SaveRootResolver.h"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace Horo::Runtime {
    namespace {
        constexpr std::string_view kProductUuid = "00112233-4455-6677-8899-aabbccddeeff";

        class FixedEnvironment final : public ProcessService {
        public:
            [[nodiscard]] ProcessMetadata CurrentProcess() const override {
                return {.id = 1, .executableName = "save-root-test"};
            }

            [[nodiscard]] std::optional<std::string> EnvironmentValue(const std::string_view name) const override {
                ++environmentLookups;
                if (name == "LOCALAPPDATA")
                    return localApplicationData;
                if (name == "HOME")
                    return home;
                if (name == "XDG_STATE_HOME")
                    return xdgStateHome;
                return std::nullopt;
            }

            std::optional<std::string> localApplicationData;
            std::optional<std::string> home;
            std::optional<std::string> xdgStateHome;
            mutable std::size_t environmentLookups{0};
        };

        class TemporaryDirectory final {
        public:
            TemporaryDirectory() {
                static std::atomic_uint64_t next{0};
                const auto base = std::filesystem::temp_directory_path();
                for (std::uint64_t attempt = 0; attempt < 256; ++attempt) {
                    path_ = base / ("horo-save-root-" + std::to_string(next.fetch_add(1)));
                    std::error_code error;
                    if (std::filesystem::create_directory(path_, error))
                        return;
                }
                FAIL("Could not create a unique save-root test directory");
            }

            ~TemporaryDirectory() {
                std::error_code ignored;
                std::filesystem::permissions(path_, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, ignored);
                std::filesystem::remove_all(path_, ignored);
            }

            [[nodiscard]] const std::filesystem::path &Path() const noexcept {
                return path_;
            }

        private:
            std::filesystem::path path_;
        };

        [[nodiscard]] ProductStorageId Product() {
            auto parsed = ProductStorageId::Parse(kProductUuid);
            REQUIRE(parsed.HasValue());
            return parsed.Value();
        }

        [[nodiscard]] ProductSaveRoot Resolve(const SaveRootResolutionRequest &request, const FixedEnvironment &environment) {
            auto resolved = ResolveProductSaveRoot(request, environment);
            REQUIRE(resolved.HasValue());
            return resolved.Value();
        }

        TEST_CASE("Save roots follow explicit platform user-state conventions", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;
            environment.localApplicationData = (temporary.Path() / "Windows State").string();
            environment.home = (temporary.Path() / "Home").string();
            environment.xdgStateHome = (temporary.Path() / "XDG State").string();

            SECTION("Windows uses LOCALAPPDATA") {
                const auto root = Resolve({.product = Product(), .platform = SaveRootPlatform::Windows}, environment);
                REQUIRE(root.CanonicalPath() ==
                        std::filesystem::canonical(temporary.Path() / "Windows State" / "Horo" / "Products" / kProductUuid));
            }
            SECTION("macOS uses HOME Application Support") {
                const auto root = Resolve({.product = Product(), .platform = SaveRootPlatform::MacOS}, environment);
                REQUIRE(root.CanonicalPath() == std::filesystem::canonical(temporary.Path() / "Home" / "Library" / "Application Support" /
                                                                           "Horo" / "Products" / kProductUuid));
            }
            SECTION("Linux prefers XDG_STATE_HOME") {
                const auto root = Resolve({.product = Product(), .platform = SaveRootPlatform::Linux}, environment);
                REQUIRE(root.CanonicalPath() ==
                        std::filesystem::canonical(temporary.Path() / "XDG State" / "horo" / "products" / kProductUuid));
            }
            SECTION("Linux falls back to HOME state") {
                environment.xdgStateHome.reset();
                const auto root = Resolve({.product = Product(), .platform = SaveRootPlatform::Linux}, environment);
                REQUIRE(root.CanonicalPath() ==
                        std::filesystem::canonical(temporary.Path() / "Home" / ".local" / "state" / "horo" / "products" / kProductUuid));
            }
        }

        TEST_CASE("Test save roots accept spaces and non-ASCII components without ambient lookup", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;
            const auto sandbox = temporary.Path() / std::filesystem::path{u8"state space-ç"};
            const auto root = Resolve({.product = Product(), .platform = SaveRootPlatform::Test, .testStateRoot = sandbox}, environment);

            REQUIRE(root.IsValid());
            REQUIRE(root.Product() == Product());
            REQUIRE(root.Platform() == SaveRootPlatform::Test);
            REQUIRE(environment.environmentLookups == 0);
            REQUIRE(root.CanonicalPath() == std::filesystem::canonical(sandbox / "horo" / "products" / kProductUuid));
        }

        TEST_CASE("Save-root configuration rejects missing relative and wrong-platform inputs", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;

            REQUIRE(ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Windows}, environment).HasError());
            environment.localApplicationData = "relative";
            REQUIRE(ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Windows}, environment).HasError());
            REQUIRE(ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Test}, environment).HasError());
            REQUIRE(ResolveProductSaveRoot({.product = Product(),
                                            .platform = static_cast<SaveRootPlatform>(255),
                                            .testStateRoot = temporary.Path()},
                                           environment)
                        .ErrorValue()
                        .code.Value() == SaveErrors::SaveRootPlatformUnsupported.code.Value());
            REQUIRE(
                ResolveProductSaveRoot({.product = {}, .platform = SaveRootPlatform::Test, .testStateRoot = temporary.Path()}, environment)
                    .ErrorValue()
                    .code.Value() == SaveErrors::IdentityInvalid.code.Value());
        }

        TEST_CASE("Save-root containment rejects redirected and unexpected entries", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;
            const auto sandbox = temporary.Path() / "approved";
            const auto outside = temporary.Path() / "outside";
            REQUIRE(std::filesystem::create_directories(outside));
            REQUIRE(std::filesystem::create_directories(sandbox));

            std::error_code linkError;
            std::filesystem::create_directory_symlink(outside, sandbox / "horo", linkError);
            if (!linkError) {
                const auto linked =
                    ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Test, .testStateRoot = sandbox},
                                           environment);
                REQUIRE(linked.HasError());
                REQUIRE(linked.ErrorValue().code.Value() == SaveErrors::SaveRootContainmentViolation.code.Value());
                REQUIRE(linked.ErrorValue().message.find(temporary.Path().string()) == std::string::npos);
            }

            std::filesystem::remove(sandbox / "horo", linkError);
            std::ofstream unexpected{sandbox / "horo"};
            REQUIRE(unexpected.good());
            unexpected.close();
            const auto fileEntry =
                ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Test, .testStateRoot = sandbox}, environment);
            REQUIRE(fileEntry.HasError());
            REQUIRE(fileEntry.ErrorValue().code.Value() == SaveErrors::SaveRootContainmentViolation.code.Value());
        }

        TEST_CASE("Save-root filesystem failures expose safe diagnostics without native paths", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;
            const auto blockingFile = temporary.Path() / "not-a-directory";
            std::ofstream output{blockingFile};
            REQUIRE(output.good());
            output.close();

            const auto failed = ResolveProductSaveRoot({.product = Product(),
                                                        .platform = SaveRootPlatform::Test,
                                                        .testStateRoot = blockingFile / "denied child"},
                                                       environment);
            REQUIRE(failed.HasError());
            REQUIRE(failed.ErrorValue().code.Value() == SaveErrors::SaveRootUnavailable.code.Value());
            REQUIRE(failed.ErrorValue().message.find(temporary.Path().string()) == std::string::npos);
            REQUIRE(failed.ErrorValue().message.find("denied child") == std::string::npos);
        }

#ifndef _WIN32
        TEST_CASE("Save-root creation rejects an unwritable approved state directory", "[unit][save][root]") {
            TemporaryDirectory temporary;
            FixedEnvironment environment;
            const auto denied = temporary.Path() / "denied";
            REQUIRE(std::filesystem::create_directory(denied));
            std::filesystem::permissions(denied, std::filesystem::perms::none);

            const auto failed =
                ResolveProductSaveRoot({.product = Product(), .platform = SaveRootPlatform::Test, .testStateRoot = denied}, environment);
            std::filesystem::permissions(denied, std::filesystem::perms::owner_all);

            REQUIRE(failed.HasError());
            REQUIRE(failed.ErrorValue().code.Value() == SaveErrors::SaveRootUnavailable.code.Value());
            REQUIRE(failed.ErrorValue().message.find(temporary.Path().string()) == std::string::npos);
        }
#endif
    }  // namespace
}  // namespace Horo::Runtime
