#pragma once

#include <filesystem>
#include <string_view>

namespace Monolith::Tests {
inline std::filesystem::path SecureTempBase() {
  namespace fs = std::filesystem;

  std::error_code ec;
  fs::path base = fs::temp_directory_path(ec);
  if (ec || base.empty()) {
    ec.clear();
    base = fs::current_path(ec);
    if (ec || base.empty())
      base = fs::path(".");
  }

  fs::path root = base / "horo_engine_tests_secure";
  ec.clear();
  if (fs::exists(root, ec) && fs::is_symlink(root, ec)) {
    root = base / "horo_engine_tests_secure_fallback";
  }

  ec.clear();
  fs::create_directories(root, ec);

#if defined(_WIN32)
  // Best-effort on Windows; ACL handling is platform-specific.
  fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ec);
#else
  fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace, ec);
#endif

  return root;
}

inline std::filesystem::path SecureTempPath(std::string_view name) {
  return SecureTempBase() / std::filesystem::path(name);
}
} // namespace Monolith::Tests
