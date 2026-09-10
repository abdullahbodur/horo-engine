#include "Horo/Platform/ConfigurationFileStore.h"

#include "Horo/Platform/PlatformErrors.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <span>
#include <string_view>
#include <utility>

namespace Horo {
    namespace {
        [[nodiscard]] Error StoreError(const ErrorCodeDescriptor &descriptor, const std::filesystem::path &path) {
            return MakeError(descriptor, std::string{descriptor.summary} + " Path: " + path.generic_string());
        }

        [[nodiscard]] std::filesystem::path TransactionPath(const std::filesystem::path &destination, const std::string_view suffix) {
            std::filesystem::path result = destination;
            result += suffix;
            return result;
        }
    }  // namespace

    /** @copydoc ConfigurationFileStore::ConfigurationFileStore */
    ConfigurationFileStore::ConfigurationFileStore(DurableFileSystem &files) noexcept : files_(files) {}

    /** @copydoc ConfigurationFileStore::Read */
    Result<std::string> ConfigurationFileStore::Read(const std::filesystem::path &path, const ConfigurationLimits &limits) const {
        if (path.empty())
            return Result<std::string>::Failure(StoreError(PlatformErrors::ConfigurationReadFailed, path));
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return Result<std::string>::Failure(StoreError(PlatformErrors::ConfigurationReadFailed, path));

        std::string document;
        document.reserve(std::min<std::size_t>(limits.maximumDocumentBytes, 64 * 1024));
        std::array<char, 8192> buffer{};
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize count = input.gcount();
            if (count <= 0)
                break;
            const auto byteCount = static_cast<std::size_t>(count);
            if (document.size() > limits.maximumDocumentBytes || byteCount > limits.maximumDocumentBytes - document.size())
                return Result<std::string>::Failure(StoreError(PlatformErrors::ConfigurationFileTooLarge, path));
            document.append(buffer.data(), byteCount);
        }
        if (input.bad())
            return Result<std::string>::Failure(StoreError(PlatformErrors::ConfigurationReadFailed, path));
        return Result<std::string>::Success(std::move(document));
    }

    /** @copydoc ConfigurationFileStore::Write */
    Result<void> ConfigurationFileStore::Write(const std::filesystem::path &path, const ConfigurationSnapshot &snapshot,
                                               const ConfigurationLimits &limits) const {
        if (path.empty())
            return Result<void>::Failure(StoreError(PlatformErrors::ConfigurationWriteFailed, path));
        const std::string document = snapshot.ToJson();
        if (document.size() > limits.maximumDocumentBytes)
            return Result<void>::Failure(StoreError(PlatformErrors::ConfigurationFileTooLarge, path));
        const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
        const std::filesystem::path lockPath = TransactionPath(path, ".lock");
        const std::filesystem::path preparedPath = TransactionPath(path, ".tmp");

        if (Result<ExclusiveFileLock> acquired = files_.TryAcquireExclusive(lockPath, "horo.configuration"); acquired.HasError()) {
            return Result<void>::Failure(acquired.ErrorValue());
        } else {
            Result<std::uint64_t> available = files_.AvailableBytes(parent);
            if (available.HasError())
                return Result<void>::Failure(available.ErrorValue());
            if (available.Value() < document.size())
                return Result<void>::Failure(StoreError(PlatformErrors::ConfigurationWriteFailed, path));

            const std::span<const char> characters{document.data(), document.size()};
            if (Result<void> written = files_.WriteDurable(preparedPath, std::as_bytes(characters)); written.HasError()) {
                static_cast<void>(files_.RemoveDurable(preparedPath));
                return Result<void>::Failure(written.ErrorValue());
            }
            if (Result<void> replaced = files_.AtomicReplace(preparedPath, path); replaced.HasError()) {
                static_cast<void>(files_.RemoveDurable(preparedPath));
                return Result<void>::Failure(replaced.ErrorValue());
            }
            return Result<void>::Success();
        }
    }
}  // namespace Horo
