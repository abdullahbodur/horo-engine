#include "Horo/Assets/AssetProvider.h"
#include "../AssetErrors.h"
#include "AssetProviderRead.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace Horo::Assets
{
namespace
{
template <typename T> [[nodiscard]] Result<T> Failure(const ErrorCodeDescriptor &descriptor, std::string message = {})
{
    return Result<T>::Failure(MakeError(descriptor, std::move(message)));
}

[[nodiscard]] bool IsTerminal(const AssetLoadState state) noexcept
{
    return state == AssetLoadState::Succeeded || state == AssetLoadState::Failed || state == AssetLoadState::Cancelled;
}

struct AssetLoadControl
{
    std::atomic<JobSystem *> jobs{};
};
} // namespace

/** @copydoc Internal::ReadExactArtifact */
Result<std::vector<std::uint8_t>> Internal::ReadExactArtifact(std::istream &input, const std::size_t expectedBytes,
                                                              const CancellationToken &cancellation)
{
    std::vector<std::uint8_t> bytes(expectedBytes);
    constexpr std::size_t kChunkBytes = 64U * 1024U;
    std::size_t offset{};
    while (offset < bytes.size())
    {
        if (cancellation.IsCancellationRequested())
            return Failure<std::vector<std::uint8_t>>(AssetErrors::LoadCancelled);
        const std::size_t count = std::min(kChunkBytes, bytes.size() - offset);
        input.read(reinterpret_cast<char *>(bytes.data() + offset), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count))
            return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderReadFailed,
                                                      "Cooked artifact changed or was truncated during the read.");
        offset += count;
    }
    return Result<std::vector<std::uint8_t>>::Success(std::move(bytes));
}

/** @copydoc FilesystemAssetProvider::FilesystemAssetProvider */
FilesystemAssetProvider::FilesystemAssetProvider(std::filesystem::path cookedRoot, const AssetProviderLimits limits)
    : cookedRoot_(std::move(cookedRoot)), limits_(limits)
{
}

/** @copydoc IAssetProvider::Exists */
Result<bool> FilesystemAssetProvider::Exists(const AssetId id, const CancellationToken &cancellation) const
{
    if (cancellation.IsCancellationRequested())
        return Failure<bool>(AssetErrors::LoadCancelled);
    if (!id.IsValid())
        return Failure<bool>(AssetErrors::IdentityInvalid);
    const std::filesystem::path path = cookedRoot_ / (id.ToString() + ".cooked");
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return Result<bool>::Success(false);
    if (error)
        return Failure<bool>(AssetErrors::ProviderReadFailed, error.message());
    return Result<bool>::Success(std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status));
}

/** @copydoc IAssetProvider::Load */
Result<std::vector<std::uint8_t>> FilesystemAssetProvider::Load(const AssetId id,
                                                                const CancellationToken &cancellation) const
{
    const Result<bool> exists = Exists(id, cancellation);
    if (exists.HasError())
        return Result<std::vector<std::uint8_t>>::Failure(exists.ErrorValue());
    if (!exists.Value())
        return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderNotFound);
    const std::filesystem::path path = cookedRoot_ / (id.ToString() + ".cooked");
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
        return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderReadFailed, error.message());
    if (size > limits_.maximumAssetBytes)
        return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderTooLarge);
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderReadFailed);
    return Internal::ReadExactArtifact(input, size, cancellation);
}

struct MemoryAssetProvider::State
{
    mutable std::mutex mutex;
    std::unordered_map<AssetId, std::vector<std::uint8_t>, AssetIdHash> assets;
};

MemoryAssetProvider::MemoryAssetProvider() : state_(std::make_unique<State>())
{
}
MemoryAssetProvider::~MemoryAssetProvider() = default;

/** @copydoc MemoryAssetProvider::Insert */
void MemoryAssetProvider::Insert(const AssetId id, std::vector<std::uint8_t> bytes)
{
    std::scoped_lock lock{state_->mutex};
    state_->assets.insert_or_assign(id, std::move(bytes));
}

/** @copydoc MemoryAssetProvider::Remove */
void MemoryAssetProvider::Remove(const AssetId id)
{
    std::scoped_lock lock{state_->mutex};
    state_->assets.erase(id);
}

/** @copydoc IAssetProvider::Exists */
Result<bool> MemoryAssetProvider::Exists(const AssetId id, const CancellationToken &cancellation) const
{
    if (cancellation.IsCancellationRequested())
        return Failure<bool>(AssetErrors::LoadCancelled);
    std::scoped_lock lock{state_->mutex};
    return Result<bool>::Success(state_->assets.contains(id));
}

/** @copydoc IAssetProvider::Load */
Result<std::vector<std::uint8_t>> MemoryAssetProvider::Load(const AssetId id,
                                                            const CancellationToken &cancellation) const
{
    if (cancellation.IsCancellationRequested())
        return Failure<std::vector<std::uint8_t>>(AssetErrors::LoadCancelled);
    std::scoped_lock lock{state_->mutex};
    const auto found = state_->assets.find(id);
    if (found == state_->assets.end())
        return Failure<std::vector<std::uint8_t>>(AssetErrors::ProviderNotFound);
    return Result<std::vector<std::uint8_t>>::Success(found->second);
}

struct AssetLoadHandle::Request
{
    AssetId id;
    AssetRegistryRevision revision;
    std::optional<AssetRecord> record;
    std::shared_ptr<AssetLoadControl> control;
    std::shared_ptr<JobHandle> job;
    std::atomic<AssetLoadState> state{AssetLoadState::Queued};
    std::mutex resultMutex;
    std::optional<Result<AssetLoadResult>> result;
    bool consumed{};
};

/** @copydoc AssetLoadHandle::State */
AssetLoadState AssetLoadHandle::State() const noexcept
{
    return request_ ? request_->state.load(std::memory_order_acquire) : AssetLoadState::Failed;
}

/** @copydoc AssetLoadHandle::RequestCancel */
Result<void> AssetLoadHandle::RequestCancel()
{
    if (!request_ || !request_->job)
        return Result<void>::Failure(MakeError(AssetErrors::LoadShutdown));
    JobSystem *jobs = request_->control ? request_->control->jobs.load(std::memory_order_acquire) : nullptr;
    if (!jobs)
        return Result<void>::Failure(MakeError(AssetErrors::LoadShutdown));
    AssetLoadState expected = AssetLoadState::Queued;
    if (request_->state.compare_exchange_strong(expected, AssetLoadState::Cancelled, std::memory_order_acq_rel))
    {
        std::scoped_lock lock{request_->resultMutex};
        request_->result = Result<AssetLoadResult>::Failure(MakeError(AssetErrors::LoadCancelled));
    }
    const Result<void> cancelled = jobs->RequestCancel(request_->job->Id());
    if (cancelled.HasError() && !IsTerminal(request_->state.load(std::memory_order_acquire)))
        return cancelled;
    return Result<void>::Success();
}

/** @copydoc AssetLoadHandle::Wait */
Result<void> AssetLoadHandle::Wait() const
{
    if (!request_ || !request_->job)
        return Result<void>::Failure(MakeError(AssetErrors::LoadShutdown));
    const Result<void> waited = request_->job->Wait();
    if (waited.HasError() && request_->state.load(std::memory_order_acquire) == AssetLoadState::Queued)
        request_->state.store(AssetLoadState::Cancelled, std::memory_order_release);
    return waited.HasError() && request_->state.load(std::memory_order_acquire) != AssetLoadState::Cancelled
               ? waited
               : Result<void>::Success();
}

/** @copydoc AssetLoadHandle::TakeResult */
Result<AssetLoadResult> AssetLoadHandle::TakeResult()
{
    if (!request_)
        return Failure<AssetLoadResult>(AssetErrors::LoadShutdown);
    if (!IsTerminal(request_->state.load(std::memory_order_acquire)))
        return Failure<AssetLoadResult>(AssetErrors::LoadNotReady);
    std::scoped_lock lock{request_->resultMutex};
    if (request_->consumed)
        return Failure<AssetLoadResult>(AssetErrors::LoadConsumed);
    request_->consumed = true;
    if (!request_->result)
        return Failure<AssetLoadResult>(AssetErrors::LoadCancelled);
    return std::move(*request_->result);
}

struct AssetLoadService::State
{
    State(JobSystem &jobSystem, const IAssetProvider &assetProvider, const std::size_t limit)
        : jobs(jobSystem), provider(assetProvider), maximumOutstanding(limit),
          control(std::make_shared<AssetLoadControl>())
    {
        control->jobs.store(&jobs, std::memory_order_release);
    }
    JobSystem &jobs;
    const IAssetProvider &provider;
    std::size_t maximumOutstanding;
    std::shared_ptr<AssetLoadControl> control;
    std::mutex mutex;
    std::vector<std::shared_ptr<AssetLoadHandle::Request>> requests;
    bool accepting{true};
};

AssetLoadService::AssetLoadService(JobSystem &jobs, const IAssetProvider &provider,
                                   const std::size_t maximumOutstanding)
    : state_(std::make_unique<State>(jobs, provider, maximumOutstanding))
{
}

AssetLoadService::~AssetLoadService()
{
    Shutdown();
}

/** @copydoc AssetLoadService::LoadAsync */
Result<AssetLoadHandle> AssetLoadService::LoadAsync(const AssetRegistrySnapshot &snapshot, const AssetId id)
{
    const AssetRecord *record = snapshot.Find(id);
    if (!record)
        return Failure<AssetLoadHandle>(AssetErrors::ProviderNotFound,
                                        "Asset identity is not present in the captured registry snapshot.");
    static_cast<void>(record);
    std::scoped_lock lock{state_->mutex};
    if (!state_->accepting)
        return Failure<AssetLoadHandle>(AssetErrors::LoadShutdown);
    std::erase_if(state_->requests,
                  [](const auto &request) { return IsTerminal(request->state.load(std::memory_order_acquire)); });
    if (state_->requests.size() >= state_->maximumOutstanding)
        return Failure<AssetLoadHandle>(AssetErrors::LoadQueueFull);

    auto request = std::make_shared<AssetLoadHandle::Request>();
    request->id = id;
    request->revision = snapshot.Revision();
    request->record = *record;
    request->control = state_->control;
    Result<JobHandle> submitted = state_->jobs.Submit(
        JobDescriptor{}, [request, provider = &state_->provider](const CancellationToken &cancellation) {
            AssetLoadState expected = AssetLoadState::Queued;
            if (!request->state.compare_exchange_strong(expected, AssetLoadState::Running, std::memory_order_acq_rel))
                return;
            try
            {
                Result<std::vector<std::uint8_t>> loaded = provider->Load(request->id, cancellation);
                std::scoped_lock resultLock{request->resultMutex};
                if (loaded.HasError())
                {
                    const bool cancelled = cancellation.IsCancellationRequested() ||
                                           loaded.ErrorValue().code.Value() == "asset.load.cancelled";
                    request->result = Result<AssetLoadResult>::Failure(loaded.ErrorValue());
                    request->state.store(cancelled ? AssetLoadState::Cancelled : AssetLoadState::Failed,
                                         std::memory_order_release);
                }
                else
                {
                    request->result = Result<AssetLoadResult>::Success(
                        AssetLoadResult{request->id, request->revision, std::move(loaded).Value()});
                    request->state.store(AssetLoadState::Succeeded, std::memory_order_release);
                }
            }
            catch (...)
            {
                std::scoped_lock resultLock{request->resultMutex};
                request->result = Result<AssetLoadResult>::Failure(MakeError(AssetErrors::ProviderReadFailed));
                request->state.store(AssetLoadState::Failed, std::memory_order_release);
            }
        });
    if (submitted.HasError())
    {
        const ErrorCodeDescriptor &descriptor = submitted.ErrorValue().code.Value() == "job.queue_full"
                                                    ? AssetErrors::LoadQueueFull
                                                    : AssetErrors::LoadShutdown;
        return Failure<AssetLoadHandle>(descriptor, submitted.ErrorValue().message);
    }
    request->job = std::make_shared<JobHandle>(std::move(submitted).Value());
    state_->requests.push_back(request);
    return Result<AssetLoadHandle>::Success(AssetLoadHandle{std::move(request)});
}

/** @copydoc AssetLoadService::Shutdown */
void AssetLoadService::Shutdown() noexcept
{
    if (!state_)
        return;
    std::vector<std::shared_ptr<AssetLoadHandle::Request>> requests;
    {
        std::scoped_lock lock{state_->mutex};
        if (!state_->accepting && state_->requests.empty())
            return;
        state_->accepting = false;
        requests = state_->requests;
    }
    for (const auto &request : requests)
    {
        AssetLoadHandle handle{request};
        static_cast<void>(handle.RequestCancel());
    }
    for (const auto &request : requests)
        if (request->job)
            static_cast<void>(request->job->Wait());
    state_->control->jobs.store(nullptr, std::memory_order_release);
    std::scoped_lock lock{state_->mutex};
    state_->requests.clear();
}
} // namespace Horo::Assets
