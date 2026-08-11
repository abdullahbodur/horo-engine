#include "Horo/Application/GameplayBuildService.h"

#include "Horo/Foundation/PathUtils.h"
#include "Horo/Foundation/Sha256.h"
#include "Horo/Gameplay/GameModule.h"
#include "Horo/Gameplay/GameModuleHost.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <set>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Horo::Application {
    namespace {
        const ErrorDomainId Domain{"horo.application.gameplay_build"};
        const ErrorCodeDescriptor InvalidRequest{Domain,
                                                 ErrorCode{"invalid_request"},
                                                 ErrorSeverity::Error,
                                                 "Gameplay build request is invalid.",
                                                 "Verify the project and SDK paths.",
                                                 false,
                                                 true};
        const ErrorCodeDescriptor BuildFailed{Domain,
                                              ErrorCode{"build_failed"},
                                              ErrorSeverity::Error,
                                              "Gameplay module build failed.",
                                              "Open Build Output and fix the reported source error.",
                                              true,
                                              true};
        const ErrorCodeDescriptor ValidationFailed{Domain,
                                                   ErrorCode{"validation_failed"},
                                                   ErrorSeverity::Error,
                                                   "Gameplay artifact validation failed.",
                                                   "Rebuild the module with the active Horo gameplay SDK.",
                                                   true,
                                                   true};
        const ErrorCodeDescriptor Cancelled{Domain,
                                            ErrorCode{"cancelled"},
                                            ErrorSeverity::Info,
                                            "Gameplay build was cancelled.",
                                            "Start the build again when ready.",
                                            true,
                                            false};
        const ErrorCodeDescriptor TimedOut{Domain,
                                           ErrorCode{"timed_out"},
                                           ErrorSeverity::Error,
                                           "Gameplay build timed out.",
                                           "Inspect the toolchain and retry the build.",
                                           true,
                                           true};

        [[nodiscard]] bool IsTerminal(const GameplayBuildState state) noexcept {
            return state == GameplayBuildState::Succeeded || state == GameplayBuildState::Failed ||
                   state == GameplayBuildState::Cancelled || state == GameplayBuildState::TimedOut;
        }

        [[nodiscard]] bool NativeInputExtension(const std::filesystem::path &path) {
            static const std::set<std::string, std::less<>> extensions{".c",   ".cc",  ".cpp", ".cxx", ".h",    ".hh",
                                                                       ".hpp", ".hxx", ".inl", ".ixx", ".cppm", ".cmake"};
            return extensions.contains(path.extension().string());
        }

        [[nodiscard]] bool HasPathPrefix(const std::filesystem::path &root, const std::filesystem::path &candidate) {
            return Horo::Foundation::Paths::HasPathPrefix(root, candidate);
        }

        struct CompilerIdentity {
            std::filesystem::path canonicalPath;
            std::uintmax_t size{};
            std::int64_t lastWrite{};
            std::string nativeFileIdentity;
            std::string binaryHash;
        };

        [[nodiscard]] Result<std::string> HashFile(const std::filesystem::path &path, const std::uintmax_t maximumBytes) {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size > maximumBytes)
                return Result<std::string>::Failure(MakeError(InvalidRequest, "File exceeds the bounded SHA-256 budget."));
            std::ifstream stream{path, std::ios::binary};
            std::vector<char> bytes(size);
            stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if ((!stream && !stream.eof()) || static_cast<std::uintmax_t>(stream.gcount()) != size)
                return Result<std::string>::Failure(MakeError(InvalidRequest, "File could not be hashed."));
            return Result<std::string>::Success(FormatSha256(ComputeSha256(std::as_bytes(std::span{bytes}))));
        }

        [[nodiscard]] Result<CompilerIdentity> ResolveCompilerIdentity(const std::filesystem::path &compiler) {
            std::error_code error;
            CompilerIdentity identity;
            identity.canonicalPath = std::filesystem::canonical(compiler, error);
            if (error || !std::filesystem::is_regular_file(identity.canonicalPath, error))
                return Result<CompilerIdentity>::Failure(MakeError(InvalidRequest, "Selected C++ compiler is unavailable."));
            identity.size = std::filesystem::file_size(identity.canonicalPath, error);
            const auto writeTime = std::filesystem::last_write_time(identity.canonicalPath, error);
            if (error || identity.size > 512U * 1024U * 1024U)
                return Result<CompilerIdentity>::Failure(MakeError(InvalidRequest, "Selected C++ compiler identity is unreadable."));
            identity.lastWrite = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
#if defined(_WIN32)
            HANDLE handle =
                CreateFileW(identity.canonicalPath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            BY_HANDLE_FILE_INFORMATION information{};
            if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &information)) {
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
                return Result<CompilerIdentity>::Failure(MakeError(InvalidRequest, "Compiler file identity is unavailable."));
            }
            CloseHandle(handle);
            identity.nativeFileIdentity = std::to_string(information.dwVolumeSerialNumber) + ":" +
                                          std::to_string(information.nFileIndexHigh) + ":" + std::to_string(information.nFileIndexLow);
#else
            struct stat information{};
            if (stat(identity.canonicalPath.c_str(), &information) != 0)
                return Result<CompilerIdentity>::Failure(MakeError(InvalidRequest, "Compiler file identity is unavailable."));
            identity.nativeFileIdentity = std::format("{} : {}", static_cast<std::uintmax_t>(information.st_dev), static_cast<std::uintmax_t>(information.st_ino));
#endif
            const std::string cacheKey = identity.canonicalPath.generic_string() + "\n" + identity.nativeFileIdentity + "\n" +
                                         std::to_string(identity.size) + "\n" + std::to_string(identity.lastWrite);
            static std::mutex cacheMutex;
            static std::unordered_map<std::string, std::string> hashes;
            {
                std::lock_guard lock(cacheMutex);
                if (const auto found = hashes.find(cacheKey); found != hashes.end()) {
                    identity.binaryHash = found->second;
                    return Result<CompilerIdentity>::Success(std::move(identity));
                }
            }
            Result<std::string> binaryHash = HashFile(identity.canonicalPath, 512U * 1024U * 1024U);
            if (binaryHash.HasError())
                return Result<CompilerIdentity>::Failure(binaryHash.ErrorValue());
            identity.binaryHash = std::move(binaryHash).Value();
            {
                std::lock_guard lock(cacheMutex);
                hashes.try_emplace(cacheKey, identity.binaryHash);
            }
            return Result<CompilerIdentity>::Success(std::move(identity));
        }

        [[nodiscard]] Result<std::string> ComputeInputHash(const GameplayBuildRequest &request) {
            std::error_code error;
            const std::filesystem::path root = std::filesystem::weakly_canonical(request.projectRoot, error);
            if (error || !std::filesystem::is_directory(root))
                return Result<std::string>::Failure(MakeError(InvalidRequest, "Project root is unavailable."));

            std::vector<std::filesystem::path> inputs;
            const std::filesystem::path cmakeLists = root / "CMakeLists.txt";
            if (std::filesystem::is_regular_file(cmakeLists, error))
                inputs.push_back(cmakeLists);
            for (const char *directoryName : {"cmake", "source", "include"}) {
                const std::filesystem::path directory = root / directoryName;
                if (!std::filesystem::is_directory(directory, error)) {
                    error.clear();
                    continue;
                }
                for (std::filesystem::recursive_directory_iterator iterator{directory, error}, end; iterator != end && !error;
                     iterator.increment(error)) {
                    if (iterator->is_symlink(error))
                        return Result<std::string>::Failure(MakeError(InvalidRequest, "Gameplay build inputs may not use symlinks."));
                    if (iterator->is_regular_file(error) && NativeInputExtension(iterator->path()))
                        inputs.push_back(iterator->path());
                }
                if (error)
                    return Result<std::string>::Failure(MakeError(InvalidRequest, "Gameplay build inputs could not be enumerated."));
            }
            const std::filesystem::path resolvedManifest = root / ".horo/local/gameplay_build_inputs.txt";
            if (std::ifstream resolvedInputs{resolvedManifest, std::ios::binary}) {
                std::string relativeInput;
                while (std::getline(resolvedInputs, relativeInput)) {
                    if (relativeInput.empty())
                        continue;
                    const std::filesystem::path unresolved{relativeInput};
                    if (unresolved.is_absolute())
                        return Result<std::string>::Failure(MakeError(InvalidRequest, "Gameplay build input must be project-relative."));
                    const std::filesystem::path canonicalInput = std::filesystem::weakly_canonical(root / unresolved, error);
                    if (error || !HasPathPrefix(root, canonicalInput) || !std::filesystem::is_regular_file(canonicalInput, error))
                        return Result<std::string>::Failure(
                            MakeError(InvalidRequest, "Resolved gameplay build input escapes the project or is unavailable."));
                    inputs.push_back(canonicalInput);
                }
                inputs.push_back(resolvedManifest);
            }
            std::ranges::sort(inputs);
            inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());

            std::string compilerHash;
            if (request.environment.cxxCompiler) {
                Result<CompilerIdentity> compiler = ResolveCompilerIdentity(*request.environment.cxxCompiler);
                if (compiler.HasError())
                    return Result<std::string>::Failure(compiler.ErrorValue());
                compilerHash = compiler.Value().binaryHash;
            }
            std::string bytes = std::string{Gameplay::CurrentGameplayBuildFingerprint()} + "\n" + request.environment.configuration + "\n" +
                                request.environment.gameplaySdkPackage.generic_string() + "\n" +
                                request.environment.cxxCompiler.value_or(std::filesystem::path{}).generic_string() + "\n" +
                                request.environment.generator.value_or("") + "\n" + request.environment.generatorPlatform.value_or("") +
                                "\n" + request.environment.generatorToolset.value_or("") + "\n" +
                                request.environment.toolchainFile.value_or(std::filesystem::path{}).generic_string() + "\n" + compilerHash +
                                "\n";
            constexpr std::uintmax_t MaximumInputBytes = 64U * 1024U * 1024U;
            for (const std::filesystem::path &path : inputs) {
                if (const std::uintmax_t size = std::filesystem::file_size(path, error);
                    error || size > MaximumInputBytes || bytes.size() + size > MaximumInputBytes)
                    return Result<std::string>::Failure(MakeError(InvalidRequest, "Gameplay build inputs exceed the bounded hash budget."));
                std::ifstream stream{path, std::ios::binary};
                if (!stream)
                    return Result<std::string>::Failure(MakeError(InvalidRequest, "Gameplay build input could not be read."));
                bytes.append(std::filesystem::relative(path, root, error).generic_string()).push_back('\0');
                bytes.append(std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{});
                bytes.push_back('\0');
            }
            return Result<std::string>::Success(FormatSha256(ComputeSha256(std::as_bytes(std::span{bytes.data(), bytes.size()}))));
        }

        [[nodiscard]] std::optional<std::string> ReadSuccessfulHash(const std::filesystem::path &root) {
            std::ifstream stream{root / ".horo/local/gameplay_build_state.json", std::ios::binary};
            if (!stream)
                return std::nullopt;
            try {
                const nlohmann::json document = nlohmann::json::parse(stream, nullptr, true, true);
                if (document.value("schemaVersion", 0) != 1 || !document.contains("successfulInputHash") ||
                    !document["successfulInputHash"].is_string())
                    return std::nullopt;
                return document["successfulInputHash"].get<std::string>();
            } catch (...) {
                return std::nullopt;
            }
        }

        [[nodiscard]] ProcessEnvironment BuildEnvironment() {
            ProcessEnvironment environment;
            environment
                .unset = {"CC", "CXX", "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET", "CMAKE_TOOLCHAIN_FILE"};
            return environment;
        }

        [[nodiscard]] std::uint64_t CurrentProcessId() noexcept {
#if defined(_WIN32)
            return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
            return static_cast<std::uint64_t>(getpid());
#endif
        }

        [[nodiscard]] std::int64_t ProcessStartedAtSeconds() noexcept {
            static const std::int64_t started =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            return started;
        }
    }  // namespace

    struct GameplayBuildService::State {
        struct Session {
            mutable std::mutex mutex;
            GameplayBuildSnapshot snapshot;
            GameplayBuildRequest request;
            std::optional<GameplayBuildRequest> pendingRequest;
            CancellationSource cancellation;
            JobId jobId{};
            std::shared_ptr<JobHandle> job;
        };

        IExternalProcessRunner *processes{};
        JobSystem *jobs{};
        DurableFileSystem *files{};
        BuildOutputStore *output{};
        OperationStore *operations{};
        mutable std::mutex mutex;
        GameplayBuildSessionId nextId{1};
        std::unordered_map<GameplayBuildSessionId, std::shared_ptr<Session>> sessions;
        std::unordered_map<std::string, GameplayBuildSessionId> activeProjects;
        bool shutdown{false};
    };

    namespace {
        void Update(const std::shared_ptr<GameplayBuildService::State::Session> &session, const GameplayBuildState state, std::string phase,
                    std::optional<Error> error = std::nullopt) {
            std::lock_guard lock(session->mutex);
            session->snapshot.state = state;
            session->snapshot.phase = std::move(phase);
            session->snapshot.error = std::move(error);
            if (IsTerminal(state))
                session->snapshot.timeoutDeadline.reset();
        }

        void UpdateOperation(GameplayBuildService::State &state, const std::shared_ptr<GameplayBuildService::State::Session> &session,
                             const OperationState operationState, const char *phase, const char *message,
                             std::optional<Error> error = std::nullopt) {
            std::optional<OperationId> id;
            {
                std::lock_guard lock(session->mutex);
                id = session->snapshot.operationId;
            }
            if (state.operations && id)
                static_cast<void>(state.operations->Update(*id, {operationState, phase, message, std::nullopt, std::move(error)}));
        }

        struct OutputBudget {
            std::size_t bytes{};
            std::size_t informationalBytes{};
            std::size_t records{};
            std::size_t informationalRecords{};
            std::size_t diagnosticRecords{};
            std::size_t suppressedBytes{};
            std::size_t suppressedLines{};
        };

        void PublishOutput(GameplayBuildService::State &state, const std::shared_ptr<GameplayBuildService::State::Session> &session,
                           OutputBudget &budget, const char *phase, ProcessOutputLine line) {
            constexpr std::size_t MaximumBytes = 8U * 1024U * 1024U;
            constexpr std::size_t MaximumRecords = 8192U;
            constexpr std::size_t DiagnosticRecordReserve = 1024U;
            constexpr std::size_t DiagnosticByteReserve = 1024U * 1024U;
            if (!state.output)
                return;
            BuildOutputRecord record{.timestampUtc = std::chrono::system_clock::now(),
                                     .status = BuildOutputStatus::Info,
                                     .phase = phase,
                                     .message = line.truncated ? line.text + " … [line truncated]" : std::move(line.text)};
            static const std::regex diagnostic{R"(^(.+):(\d+):(\d+):\s*(error|warning):\s*(.*)$)"};
            std::smatch match;
            if (std::regex_match(record.message, match, diagnostic)) {
                record.source = DiagnosticSourceLocation{match[1].str(), static_cast<std::uint32_t>(std::stoul(match[2].str())),
                                                         static_cast<std::uint32_t>(std::stoul(match[3].str()))};
                if (match[4].str() == "error")
                    record.status = BuildOutputStatus::Failed;
            }
            const bool diagnosticRecord = record.source.has_value();
            const bool byteBudgetAvailable = budget.bytes + record.message.size() <= MaximumBytes;
            const bool recordBudgetAvailable = budget.records < MaximumRecords;
            const bool classBudgetAvailable =
                diagnosticRecord ? budget.diagnosticRecords < DiagnosticRecordReserve
                                 : budget.informationalRecords < MaximumRecords - DiagnosticRecordReserve &&
                                       budget.informationalBytes + record.message.size() <= MaximumBytes - DiagnosticByteReserve;
            if (!byteBudgetAvailable || !recordBudgetAvailable || !classBudgetAvailable) {
                budget.suppressedBytes += record.message.size();
                ++budget.suppressedLines;
                return;
            }
            budget.bytes += record.message.size();
            ++budget.records;
            if (diagnosticRecord)
                ++budget.diagnosticRecords;
            else {
                budget.informationalBytes += record.message.size();
                ++budget.informationalRecords;
            }
            {
                std::lock_guard lock(session->mutex);
                record.operationId = session->snapshot.operationId;
            }
            state.output->Append(std::move(record));
        }

        struct OutputSummaryGuard final {
            GameplayBuildService::State *state{};
            std::shared_ptr<GameplayBuildService::State::Session> session;
            OutputBudget *budget{};

            ~OutputSummaryGuard() {
                if (state == nullptr || state->output == nullptr || budget == nullptr || budget->suppressedLines == 0)
                    return;
                BuildOutputRecord
                    record{.timestampUtc = std::chrono::system_clock::now(),
                           .status = BuildOutputStatus::Info,
                           .phase = "output",
                           .message = std::format("Suppressed {} build output lines ({} bytes) after the session budget was reached.",
                                                  budget->suppressedLines, budget->suppressedBytes)};
                {
                    std::lock_guard lock(session->mutex);
                    record.operationId = session->snapshot.operationId;
                }
                state->output->Append(std::move(record));
            }
        };

        [[nodiscard]] Result<void> RunPhase(GameplayBuildService::State &state,
                                            const std::shared_ptr<GameplayBuildService::State::Session> &session,
                                            const GameplayBuildState buildState, const char *phase, std::vector<std::string> arguments,
                                            const std::chrono::milliseconds timeout, OutputBudget &budget) {
            Update(session, buildState, phase);
            {
                std::lock_guard lock(session->mutex);
                session->snapshot.timeoutDeadline = std::chrono::steady_clock::now() + timeout;
            }
            UpdateOperation(state, session, OperationState::Running, phase, "Gameplay build is running.");
            ExternalProcessRequest request;
            request.executable = session->request.environment.cmakeExecutable;
            request.arguments = std::move(arguments);
            request.workingDirectory = session->request.projectRoot;
            request.environment = BuildEnvironment();
            request.timeout = timeout;
            request.gracefulTermination = session->request.timeouts.gracefulTermination;
            request.onOutput = [&state, session, &budget, phase](ProcessOutputLine line) {
                PublishOutput(state, session, budget, phase, std::move(line));
            };
            Result<ExternalProcessResult> executed = state.processes->Run(request, session->cancellation.Token());
            if (executed.HasError())
                return Result<void>::Failure(executed.ErrorValue());
            if (executed.Value().reason == ProcessTerminationReason::TimedOut)
                return Result<void>::Failure(MakeError(TimedOut));
            if (executed.Value().reason == ProcessTerminationReason::Cancelled)
                return Result<void>::Failure(MakeError(Cancelled));
            if (executed.Value().reason != ProcessTerminationReason::Exited || executed.Value().exitCode != 0)
                return Result<void>::Failure(
                    MakeError(BuildFailed, std::string{phase} + " exited with code " + std::to_string(executed.Value().exitCode) + "."));
            return Result<void>::Success();
        }

        [[nodiscard]] Result<void> ValidateAndPublish(GameplayBuildService::State &state,
                                                      const std::shared_ptr<GameplayBuildService::State::Session> &session,
                                                      const std::string &inputHash) {
            Update(session, GameplayBuildState::Validating, "validate");
            const std::filesystem::path buildRoot = session->request.projectRoot / ".horo/local/build/gameplay-debug";
            const std::filesystem::path candidate = buildRoot / "candidate_gameplay_module.json";
            std::ifstream stream{candidate, std::ios::binary};
            if (!stream)
                return Result<void>::Failure(MakeError(ValidationFailed, "Candidate manifest is missing."));
            nlohmann::json manifest;
            try {
                manifest = nlohmann::json::parse(stream, nullptr, true, true);
            } catch (...) {
                return Result<void>::Failure(MakeError(ValidationFailed, "Candidate manifest is malformed."));
            }
            if (manifest.value("schemaVersion", 0) != 1 ||
                manifest.value("buildFingerprint", "") != Gameplay::CurrentGameplayBuildFingerprint() ||
                !manifest.contains("artifactPath") || !manifest["artifactPath"].is_string())
                return Result<void>::Failure(MakeError(ValidationFailed, "Candidate manifest identity is incompatible."));
            const std::filesystem::path artifact = manifest["artifactPath"].get<std::string>();
            Gameplay::GameModuleHost host;
            auto loaded = host.LoadShadowCopy(artifact, buildRoot / "validation-shadow", Gameplay::CurrentGameplayBuildFingerprint());
            if (loaded.HasError())
                return Result<void>::Failure(loaded.ErrorValue());
            std::unique_ptr<Gameplay::LoadedGameModule> validatedModule = std::move(loaded).Value();
            validatedModule.reset();

            Result<std::string> postHash = ComputeInputHash(session->request);
            if (postHash.HasError())
                return Result<void>::Failure(postHash.ErrorValue());
            if (postHash.Value() != inputHash)
                return Result<void>::Failure(MakeError(BuildFailed, "Gameplay sources changed during the build."));

            const std::filesystem::path local = session->request.projectRoot / ".horo/local";
            Result<std::string> artifactHash = HashFile(artifact, 512U * 1024U * 1024U);
            Result<std::string> manifestHash = HashFile(candidate, 1024U * 1024U);
            if (artifactHash.HasError() || manifestHash.HasError())
                return Result<void>::Failure(artifactHash.HasError() ? artifactHash.ErrorValue() : manifestHash.ErrorValue());
            std::error_code fileError;
            const std::uintmax_t artifactSize = std::filesystem::file_size(artifact, fileError);
            const auto artifactWriteTime = std::filesystem::last_write_time(artifact, fileError);
            if (fileError)
                return Result<void>::Failure(MakeError(ValidationFailed, "Artifact file identity is unavailable."));
            std::string resolvedInputManifestHash;
            const std::filesystem::path resolvedInputManifest = local / "gameplay_build_inputs.txt";
            if (std::filesystem::is_regular_file(resolvedInputManifest, fileError)) {
                Result<std::string> resolvedHash = HashFile(resolvedInputManifest, 1024U * 1024U);
                if (resolvedHash.HasError())
                    return Result<void>::Failure(resolvedHash.ErrorValue());
                resolvedInputManifestHash = std::move(resolvedHash).Value();
            }
            nlohmann::json compiler = nullptr;
            if (session->request.environment.cxxCompiler) {
                Result<CompilerIdentity> identity = ResolveCompilerIdentity(*session->request.environment.cxxCompiler);
                if (identity.HasError())
                    return Result<void>::Failure(identity.ErrorValue());
                compiler = {{"canonicalPath", identity.Value().canonicalPath.string()},
                            {"nativeFileIdentity", identity.Value().nativeFileIdentity},
                            {"size", identity.Value().size},
                            {"lastWrite", identity.Value().lastWrite},
                            {"binarySha256", identity.Value().binaryHash}};
            }
            const nlohmann::json toolchain{{"generator", session->request.environment.generator.value_or("")},
                                           {"generatorPlatform", session->request.environment.generatorPlatform.value_or("")},
                                           {"generatorToolset", session->request.environment.generatorToolset.value_or("")},
                                           {"toolchainFile",
                                            session->request.environment.toolchainFile.value_or(std::filesystem::path{}).string()},
                                           {"configuration", session->request.environment.configuration}};
            const nlohmann::json buildState{{"schemaVersion", 1},
                                            {"successfulInputHash", inputHash},
                                            {"sdkFingerprint", Gameplay::CurrentGameplayBuildFingerprint()},
                                            {"compiler", std::move(compiler)},
                                            {"toolchain", toolchain},
                                            {"configuration", session->request.environment.configuration},
                                            {"resolvedInputManifestHash", resolvedInputManifestHash},
                                            {"artifactIdentity",
                                             {{"path", artifact.string()},
                                              {"size", artifactSize},
                                              {"lastWrite", static_cast<std::int64_t>(artifactWriteTime.time_since_epoch().count())},
                                              {"sha256", artifactHash.Value()}}},
                                            {"moduleManifestHash", manifestHash.Value()},
                                            {"descriptorRevision", manifest.value("descriptorRevision", 0)}};
            const std::string encoded = buildState.dump(2) + "\n";
            const std::filesystem::path manifestTemporary = local / "gameplay_module.json.next";
            if (Result<void> copied = state.files->CopyDurable(candidate, manifestTemporary); copied.HasError())
                return copied;
            const std::filesystem::path stateTemporary = local / "gameplay_build_state.json.next";
            if (Result<void> stateWritten = state.files->WriteDurable(stateTemporary, std::as_bytes(std::span{encoded}));
                stateWritten.HasError())
                return stateWritten;
            const std::filesystem::path publishedManifest = local / "gameplay_module.json";
            const std::filesystem::path manifestBackup = local / "gameplay_module.json.previous";
            const bool hadPublishedManifest = std::filesystem::is_regular_file(publishedManifest, fileError);
            if (hadPublishedManifest) {
                Result<void> backedUp = state.files->CopyDurable(publishedManifest, manifestBackup);
                if (backedUp.HasError())
                    return backedUp;
            }
            Result<void> published = state.files->AtomicReplace(manifestTemporary, publishedManifest);
            if (published.HasError())
                return published;
            Result<void> statePublished = state.files->AtomicReplace(stateTemporary, local / "gameplay_build_state.json");
            if (statePublished.HasError()) {
                if (hadPublishedManifest)
                    static_cast<void>(state.files->AtomicReplace(manifestBackup, publishedManifest));
                else
                    static_cast<void>(state.files->RemoveDurable(publishedManifest));
                return statePublished;
            }
            if (hadPublishedManifest)
                static_cast<void>(state.files->RemoveDurable(manifestBackup));
            return Result<void>::Success();
        }

        Result<void> RunBuild(const std::shared_ptr<GameplayBuildService::State> &state,
                              const std::shared_ptr<GameplayBuildService::State::Session> &session) {
            Update(session, GameplayBuildState::AcquiringLock, "lock");
            const std::filesystem::path lockPath = session->request.projectRoot / ".horo/local/locks/gameplay-build.lock";
            const auto waitStarted = std::chrono::steady_clock::now();
            {
                std::lock_guard lock(session->mutex);
                session->snapshot.timeoutDeadline = waitStarted + session->request.timeouts.externalWait;
            }
            std::optional<ExclusiveFileLock> buildLock;
            while (!buildLock.has_value()) {
                if (session->cancellation.Token().IsCancellationRequested())
                    return Result<void>::Failure(MakeError(Cancelled));
                const std::string owner = "pid=" + std::to_string(CurrentProcessId()) +
                                          ";started=" + std::to_string(ProcessStartedAtSeconds()) +
                                          ";session=" + std::to_string(session->snapshot.id);
                auto acquired = state->files->TryAcquireExclusive(lockPath, owner);
                if (acquired.HasValue()) {
                    buildLock.emplace(std::move(acquired).Value());
                    std::lock_guard lock(session->mutex);
                    session->snapshot.externalLockOwner.clear();
                } else {
                    Update(session, GameplayBuildState::WaitingForExternalBuild, "waiting_external_build");
                    std::ifstream metadata{lockPath, std::ios::binary};
                    std::string externalOwner{std::istreambuf_iterator<char>{metadata}, std::istreambuf_iterator<char>{}};
                    if (externalOwner.size() > 512U)
                        externalOwner.resize(512U);
                    {
                        std::lock_guard lock(session->mutex);
                        session->snapshot.externalLockOwner = std::move(externalOwner);
                    }
                    if (std::chrono::steady_clock::now() - waitStarted >= session->request.timeouts.externalWait)
                        return Result<void>::Failure(MakeError(TimedOut, "Timed out waiting for the project gameplay build lock."));
                    std::this_thread::sleep_for(std::chrono::milliseconds{50});
                }
            }

            OutputBudget budget;
            OutputSummaryGuard outputSummary{state.get(), session, &budget};
            for (;;) {
                {
                    std::lock_guard lock(session->mutex);
                    if (session->pendingRequest) {
                        session->request = std::move(*session->pendingRequest);
                        session->pendingRequest.reset();
                        session->snapshot.pendingInputHash.reset();
                        session->snapshot.newerInputsPending = false;
                    }
                }
                Result<std::string> hash = ComputeInputHash(session->request);
                if (hash.HasError())
                    return Result<void>::Failure(hash.ErrorValue());
                {
                    std::lock_guard lock(session->mutex);
                    session->snapshot.activeInputHash = hash.Value();
                    if (!session->snapshot.pendingInputHash)
                        session->snapshot.desiredInputHash = hash.Value();
                }
                if (ReadSuccessfulHash(session->request.projectRoot) == hash.Value() &&
                    std::filesystem::is_regular_file(session->request.projectRoot / ".horo/local/gameplay_module.json"))
                    return Result<void>::Success();

                const std::filesystem::path buildRoot = session->request.projectRoot / ".horo/local/build/gameplay-debug";
                const std::filesystem::path candidateManifest = buildRoot / "candidate_gameplay_module.json";
                std::vector<std::string> configureArguments{
                    "-S",
                    session->request.projectRoot.string(),
                    "-B",
                    buildRoot.string(),
                    "-DHoroEngineGameplay_DIR=" + session->request.environment.gameplaySdkPackage.string(),
                    "-DCMAKE_BUILD_TYPE=" + session->request.environment.configuration,
                    "-DHORO_GAMEPLAY_MANIFEST_OUTPUT=" + candidateManifest.string(),
                };
                if (session->request.environment.cxxCompiler)
                    configureArguments.push_back("-DCMAKE_CXX_COMPILER=" + session->request.environment.cxxCompiler->string());
                if (session->request.environment.generator) {
                    configureArguments.emplace_back("-G");
                    configureArguments.push_back(*session->request.environment.generator);
                }
                if (session->request.environment.generatorPlatform) {
                    configureArguments.emplace_back("-A");
                    configureArguments.push_back(*session->request.environment.generatorPlatform);
                }
                if (session->request.environment.generatorToolset) {
                    configureArguments.emplace_back("-T");
                    configureArguments.push_back(*session->request.environment.generatorToolset);
                }
                if (session->request.environment.toolchainFile)
                    configureArguments.push_back("-DCMAKE_TOOLCHAIN_FILE=" + session->request.environment.toolchainFile->string());
                Result<void> configured = RunPhase(*state, session, GameplayBuildState::Configuring, "configure",
                                                   std::move(configureArguments), session->request.timeouts.configure, budget);
                if (configured.HasError())
                    return configured;
                Result<void> built = RunPhase(*state, session, GameplayBuildState::Building, "build",
                                              {"--build", buildRoot.string(), "--target", "HoroGameGameplay", "--config",
                                               session->request.environment.configuration, "--parallel"},
                                              session->request.timeouts.build, budget);
                if (built.HasError())
                    return built;
                {
                    std::lock_guard lock(session->mutex);
                    if (session->pendingRequest && session->snapshot.pendingInputHash != hash.Value())
                        continue;
                }
                Result<void> validated = ValidateAndPublish(*state, session, hash.Value());
                if (validated.HasValue())
                    return validated;
                if (validated.ErrorValue().message.find("changed during") == std::string::npos)
                    return validated;
                Result<std::string> successorHash = ComputeInputHash(session->request);
                if (successorHash.HasError())
                    return Result<void>::Failure(successorHash.ErrorValue());
                std::lock_guard lock(session->mutex);
                session->snapshot.newerInputsPending = true;
                session->snapshot.pendingInputHash = successorHash.Value();
            }
        }
    }  // namespace

    GameplayBuildService::GameplayBuildService(IExternalProcessRunner &processes, JobSystem &jobs, DurableFileSystem &files,
                                               BuildOutputStore *output, OperationStore *operations)
        : state_(std::make_shared<State>()) {
        state_->processes = &processes;
        state_->jobs = &jobs;
        state_->files = &files;
        state_->output = output;
        state_->operations = operations;
    }

    GameplayBuildService::~GameplayBuildService() {
        Shutdown();
    }

    Result<GameplayBuildSessionId> GameplayBuildService::Start(GameplayBuildRequest request) {
        Result<std::string> hash = ComputeInputHash(request);
        if (hash.HasError())
            return Result<GameplayBuildSessionId>::Failure(hash.ErrorValue());
        const std::string projectKey = std::filesystem::absolute(request.projectRoot).lexically_normal().generic_string();
        std::shared_ptr<State::Session> session;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->shutdown)
                return Result<GameplayBuildSessionId>::Failure(MakeError(Cancelled, "Gameplay build service is shut down."));
            if (const auto active = state_->activeProjects.find(projectKey); active != state_->activeProjects.end()) {
                session = state_->sessions.at(active->second);
                std::lock_guard sessionLock(session->mutex);
                if (IsTerminal(session->snapshot.state)) {
                    state_->activeProjects.erase(active);
                    session.reset();
                } else {
                    ++session->snapshot.coalescedRequestCount;
                    if (session->snapshot.desiredInputHash != hash.Value()) {
                        session->pendingRequest = request;
                        session->snapshot.pendingInputHash = hash.Value();
                        session->snapshot.newerInputsPending = true;
                        session->snapshot.desiredInputHash = hash.Value();
                    }
                    return Result<GameplayBuildSessionId>::Success(session->snapshot.id);
                }
            }
            session = std::make_shared<State::Session>();
            session->snapshot.id = state_->nextId++;
            session->snapshot.activeInputHash = hash.Value();
            session->snapshot.desiredInputHash = hash.Value();
            session->request = std::move(request);
            state_->sessions.emplace(session->snapshot.id, session);
            state_->activeProjects.emplace(projectKey, session->snapshot.id);
        }

        if (state_->operations) {
            const std::weak_ptr<State> weakState = state_;
            const GameplayBuildSessionId id = session->snapshot.id;
            session->snapshot.operationId = state_->operations->Begin({OperationKind::Build, "Build gameplay module", "queued",
                                                                       "Waiting for a build worker.", std::nullopt, true, [weakState, id] {
                if (const std::shared_ptr<State> state = weakState.lock()) {
                    std::shared_ptr<State::Session> target;
                    {
                        std::lock_guard lock(state->mutex);
                        if (const auto found = state->sessions.find(id); found != state->sessions.end())
                            target = found->second;
                    }
                    if (target)
                        target->cancellation.RequestCancellation();
                }
            }});
        }

        Result<JobHandle> submitted = state_->jobs->SubmitResult({}, [state = state_, session, projectKey](const CancellationToken &) {
            Result<void> result = RunBuild(state, session);
            GameplayBuildState terminal = GameplayBuildState::Succeeded;
            if (result.HasError()) {
                terminal = result.ErrorValue().code.Value() == TimedOut.code.Value()    ? GameplayBuildState::TimedOut
                           : result.ErrorValue().code.Value() == Cancelled.code.Value() ? GameplayBuildState::Cancelled
                                                                                        : GameplayBuildState::Failed;
                Update(session, terminal, "terminal", result.ErrorValue());
                UpdateOperation(*state, session,
                                terminal == GameplayBuildState::Cancelled ? OperationState::Cancelled : OperationState::Failed, "terminal",
                                result.ErrorValue().message.c_str(), result.ErrorValue());
            } else {
                Update(session, terminal, "complete");
                UpdateOperation(*state, session, OperationState::Succeeded, "complete", "Gameplay module built successfully.");
            }
            {
                std::lock_guard lock(state->mutex);
                const auto active = state->activeProjects.find(projectKey);
                if (active != state->activeProjects.end() && active->second == session->snapshot.id)
                    state->activeProjects.erase(active);
            }
            return result;
        });
        if (submitted.HasError())
            return Result<GameplayBuildSessionId>::Failure(submitted.ErrorValue());
        session->jobId = submitted.Value().Id();
        session->job = std::make_shared<JobHandle>(std::move(submitted).Value());
        return Result<GameplayBuildSessionId>::Success(session->snapshot.id);
    }

    std::optional<GameplayBuildSnapshot> GameplayBuildService::Query(const GameplayBuildSessionId id) const {
        std::shared_ptr<State::Session> session;
        {
            std::lock_guard lock(state_->mutex);
            const auto found = state_->sessions.find(id);
            if (found == state_->sessions.end())
                return std::nullopt;
            session = found->second;
        }
        std::lock_guard lock(session->mutex);
        return session->snapshot;
    }

    bool GameplayBuildService::RequestCancel(const GameplayBuildSessionId id) {
        std::shared_ptr<State::Session> session;
        {
            std::lock_guard lock(state_->mutex);
            const auto found = state_->sessions.find(id);
            if (found == state_->sessions.end())
                return false;
            session = found->second;
        }
        session->cancellation.RequestCancellation();
        {
            std::lock_guard lock(session->mutex);
            session->pendingRequest.reset();
            session->snapshot.pendingInputHash.reset();
            session->snapshot.newerInputsPending = false;
        }
        return true;
    }

    bool GameplayBuildService::IsUpToDate(const GameplayBuildRequest &request) const {
        const Result<std::string> hash = ComputeInputHash(request);
        return hash.HasValue() && ReadSuccessfulHash(request.projectRoot) == hash.Value() &&
               std::filesystem::is_regular_file(request.projectRoot / ".horo/local/gameplay_module.json");
    }

    void GameplayBuildService::Shutdown() noexcept {
        std::vector<std::shared_ptr<State::Session>> sessions;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->shutdown)
                return;
            state_->shutdown = true;
            for (const auto &[id, session] : state_->sessions) {
                static_cast<void>(id);
                sessions.push_back(session);
            }
        }
        for (const auto &session : sessions) {
            std::lock_guard lock(session->mutex);
            if (!IsTerminal(session->snapshot.state))
                session->cancellation.RequestCancellation();
        }
        for (const auto &session : sessions) {
            std::shared_ptr<JobHandle> job;
            {
                std::lock_guard lock(session->mutex);
                job = session->job;
            }
            if (job)
                static_cast<void>(job->Wait());
        }
    }
}  // namespace Horo::Application
