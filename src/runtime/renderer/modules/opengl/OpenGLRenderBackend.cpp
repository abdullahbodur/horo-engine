#include "OpenGLBackendInternal.h"
#include "OpenGLRenderBackendErrors.h"

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace Horo::Render
{
    namespace
    {
        constexpr std::uint32_t colorBufferBit = 0x00004000U;

        [[nodiscard]] Error MakeOpenGLError(const ErrorCodeDescriptor &descriptor, std::string message)
        {
            return MakeError(descriptor, std::move(message));
        }

        /** @brief Dispatches a viewport update through the linked production OpenGL API. */
        void ProductionViewport(const std::int32_t x, const std::int32_t y, const std::int32_t width,
                                const std::int32_t height)
        {
            glViewport(x, y, width, height);
        }

        /** @brief Dispatches a clear-color update through the linked production OpenGL API. */
        void ProductionClearColor(const float red, const float green, const float blue, const float alpha)
        {
            glClearColor(red, green, blue, alpha);
        }

        /** @brief Clears selected buffers through the linked production OpenGL API. */
        void ProductionClear(const std::uint32_t mask)
        {
            glClear(mask);
        }

        /** @brief Returns the complete production OpenGL command dispatch. */
        [[nodiscard]] Detail::OpenGLCommandFunctions ProductionFunctions() noexcept
        {
            return Detail::OpenGLCommandFunctions{
                .viewport = &ProductionViewport,
                .clearColor = &ProductionClearColor,
                .clear = &ProductionClear,
            };
        }

        /** @brief Serializes ownership of the single context retained by one presentation port. */
        struct OpenGLContextLease
        {
            bool claimed{false};
        };

        /** @brief OpenGL backend owning one presentation-port context lifecycle. */
        class OpenGLRenderBackend final : public IRenderBackend
        {
        public:
            OpenGLRenderBackend(IOpenGLPresentationPort& presentationPort, const OpenGLBackendOptions options,
                                const Detail::OpenGLCommandFunctions functions,
                                std::shared_ptr<OpenGLContextLease> contextLease) noexcept
                : presentationPort_(&presentationPort), options_(options), functions_(functions),
                  contextLease_(std::move(contextLease))
            {
            }

            /** @brief Releases a remaining OpenGL context as a lifecycle fallback. */
            ~OpenGLRenderBackend() override
            {
                Shutdown();
            }

            /** @copydoc IRenderBackend::Initialize */
            Result<void> Initialize(const RenderBackendConfig& config) override
            {
                if (initialized_)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::AlreadyInitialized,
                                        "Renderer backend is already initialized."));
                }
                if (!functions_.IsValid() || options_.majorVersion == 0 || !config.IsValid())
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::InvalidConfig, "OpenGL backend configuration is invalid."));
                }
                if (contextLease_->claimed)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::PresentationInUse,
                                        "OpenGL presentation attachment is already owned by another backend."));
                }
                contextLease_->claimed = true;
                ownsContextLease_ = true;
                // Assume ownership before crossing the platform boundary. A typed failure is
                // contractually non-retaining; an exception may occur after native creation,
                // so the frontend's rollback must still call DestroyContext().
                contextCreated_ = true;
                const Result<void> created = presentationPort_->CreateContext(OpenGLContextDescriptor{
                    .majorVersion = options_.majorVersion,
                    .minorVersion = options_.minorVersion,
                    .profile = OpenGLContextProfile::Core,
                    .enableDebugContext = config.enableValidation,
                });
                if (created.HasError())
                {
                    contextCreated_ = false;
                    ReleaseContextLease();
                    return Result<void>::Failure(created.ErrorValue());
                }

                const Result<void> current = presentationPort_->MakeCurrent();
                if (current.HasError())
                {
                    DestroyContext();
                    return Result<void>::Failure(current.ErrorValue());
                }
                const Result<void> presentMode = presentationPort_->SetPresentMode(config.presentMode);
                if (presentMode.HasError())
                {
                    DestroyContext();
                    return Result<void>::Failure(presentMode.ErrorValue());
                }

                initialized_ = true;
                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::Capabilities */
            const RenderBackendCapabilities& Capabilities() const noexcept override
            {
                return capabilities_;
            }

            /** @copydoc IRenderBackend::BeginFrame */
            Result<FrameToken> BeginFrame(const FrameDescriptor& descriptor) override
            {
                if (!initialized_)
                {
                    return Result<FrameToken>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::NotInitialized, "Renderer backend is not initialized."));
                }
                if (frameActive_)
                {
                    return Result<FrameToken>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::FrameAlreadyActive, "A renderer frame is already active."));
                }
                constexpr auto maxViewportExtent = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
                if (descriptor.frameNumber == 0 || !descriptor.outputExtent.IsValid() ||
                    descriptor.outputExtent.width > maxViewportExtent || descriptor.outputExtent.height >
                    maxViewportExtent)
                {
                    return Result<FrameToken>::Failure(MakeOpenGLError(OpenGLBackendErrors::InvalidFrameDescriptor,
                                                                       "Frame number and output extent must be valid."));
                }
                if (nextFrameToken_ == std::numeric_limits<std::uint64_t>::max())
                {
                    return Result<FrameToken>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::FrameTokenExhausted, "Frame token space is exhausted."));
                }

                const Result<void> current = presentationPort_->MakeCurrent();
                if (current.HasError())
                {
                    return Result<FrameToken>::Failure(current.ErrorValue());
                }

                functions_.viewport(0, 0, static_cast<std::int32_t>(descriptor.outputExtent.width),
                                    static_cast<std::int32_t>(descriptor.outputExtent.height));
                frameActive_ = true;
                activeFrame_ = FrameToken{nextFrameToken_++};
                return Result<FrameToken>::Success(activeFrame_);
            }

            /** @copydoc IRenderBackend::Execute */
            Result<void> Execute(const RenderExecutionPlan& plan) override
            {
                const Result<void> valid = ValidatePlan(plan);
                if (valid.HasError())
                {
                    return valid;
                }

                for (const RenderPassDescriptor& pass : plan.orderedPasses)
                {
                    if (!pass.primaryOutput.has_value())
                    {
                        continue;
                    }
                    const PrimaryOutputAttachment& attachment = *pass.primaryOutput;
                    if (attachment.loadOperation == AttachmentLoadOperation::Clear)
                    {
                        const ClearColor& color = attachment.clearColor;
                        functions_.clearColor(color.red, color.green, color.blue, color.alpha);
                        functions_.clear(colorBufferBit);
                    }
                }
                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::Present */
            Result<void> Present(const FrameToken frame) override
            {
                const Result<void> state = ValidateActiveFrame(frame);
                if (state.HasError())
                {
                    return state;
                }

                const Result<void> presented = presentationPort_->SwapBuffers();
                if (presented.HasError())
                {
                    return Result<void>::Failure(presented.ErrorValue());
                }

                AbortActiveFrame();
                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::AbortFrame */
            void AbortFrame(const FrameToken frame) noexcept override
            {
                if (frameActive_ && frame == activeFrame_)
                {
                    AbortActiveFrame();
                }
            }

            /** @copydoc IRenderBackend::AbortActiveFrame */
            void AbortActiveFrame() noexcept override
            {
                frameActive_ = false;
                activeFrame_ = {};
            }

            /** @copydoc IRenderBackend::Resize */
            Result<void> Resize(const FramebufferExtent extent) override
            {
                if (!initialized_)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::NotInitialized, "Renderer backend is not initialized."));
                }
                if (!extent.IsValid())
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::InvalidExtent, "Renderer output extent must be non-zero."));
                }
                if (frameActive_)
                {
                    return Result<void>::Failure(MakeOpenGLError(OpenGLBackendErrors::FrameActive,
                                                                 "Renderer output cannot resize while a frame is active."));
                }

                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::Shutdown */
            void Shutdown() noexcept override
            {
                AbortActiveFrame();
                initialized_ = false;
                DestroyContext();
            }

        private:
            [[nodiscard]] Result<void> ValidateActiveFrame(const FrameToken frame) const
            {
                if (!initialized_)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::NotInitialized, "Renderer backend is not initialized."));
                }
                if (!frameActive_)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::NoActiveFrame, "No renderer frame is active."));
                }
                if (frame != activeFrame_)
                {
                    return Result<void>::Failure(
                        MakeOpenGLError(OpenGLBackendErrors::FrameTokenMismatch,
                                        "Frame token does not match the active frame."));
                }
                return Result<void>::Success();
            }

            [[nodiscard]] Result<void> ValidatePlan(const RenderExecutionPlan& plan) const
            {
                const Result<void> state = ValidateActiveFrame(plan.frame);
                if (state.HasError())
                {
                    return state;
                }

                for (std::size_t index = 0; index < plan.orderedPasses.size(); ++index)
                {
                    const RenderPassDescriptor& pass = plan.orderedPasses[index];
                    if (!pass.id.IsValid())
                    {
                        return Result<void>::Failure(MakeOpenGLError(OpenGLBackendErrors::InvalidExecutionPlan,
                                                                     "Execution plan contains an invalid render pass ID."));
                    }
                    if (pass.kind != RenderPassKind::Graphics)
                    {
                        return Result<void>::Failure(MakeOpenGLError(OpenGLBackendErrors::UnsupportedPassKind,
                                                                     "Initial OpenGL backend supports graphics passes only."));
                    }
                    for (std::size_t previous = 0; previous < index; ++previous)
                    {
                        if (pass.id == plan.orderedPasses[previous].id)
                        {
                            return Result<void>::Failure(MakeOpenGLError(OpenGLBackendErrors::InvalidExecutionPlan,
                                                                         "Execution plan contains duplicate render pass IDs."));
                        }
                    }
                    if (!pass.primaryOutput.has_value())
                    {
                        continue;
                    }

                    if (!pass.primaryOutput->IsValid())
                    {
                        return Result<void>::Failure(MakeOpenGLError(OpenGLBackendErrors::InvalidExecutionPlan,
                                                                     "Primary output attachment operations are invalid."));
                    }
                }
                return Result<void>::Success();
            }

            void DestroyContext() noexcept
            {
                if (contextCreated_)
                {
                    presentationPort_->DestroyContext();
                    contextCreated_ = false;
                }
                ReleaseContextLease();
            }

            void ReleaseContextLease() noexcept
            {
                if (ownsContextLease_)
                {
                    contextLease_->claimed = false;
                    ownsContextLease_ = false;
                }
            }

            IOpenGLPresentationPort* presentationPort_{nullptr};
            OpenGLBackendOptions options_{};
            Detail::OpenGLCommandFunctions functions_{};
            std::shared_ptr<OpenGLContextLease> contextLease_;
            RenderBackendCapabilities capabilities_{
                .backend = RenderBackendId{"opengl"},
                .presentsToWindow = true,
                .supportsOffscreenTargets = false,
                .supportsTimestampQueries = false,
                .supportsCompute = false,
                .supportsBindlessResources = false,
                .supportsRayTracing = false,
            };
            FrameToken activeFrame_{};
            std::uint64_t nextFrameToken_{1};
            bool initialized_{false};
            bool contextCreated_{false};
            bool ownsContextLease_{false};
            bool frameActive_{false};
        };

        /** @brief Provides inert OpenGL backend instances bound to one borrowed presentation port. */
        class OpenGLBackendProvider final : public IRenderBackendProvider
        {
        public:
            OpenGLBackendProvider(IOpenGLPresentationPort& presentationPort, const OpenGLBackendOptions options,
                                  const Detail::OpenGLCommandFunctions functions)
                : presentationPort_(&presentationPort), options_(options), functions_(functions),
                  contextLease_(std::make_shared<OpenGLContextLease>())
            {
            }

            /** @copydoc IRenderBackendProvider::Create */
            Result<std::unique_ptr<IRenderBackend>> Create() const override
            {
                return Result<std::unique_ptr<IRenderBackend>>::Success(
                    std::make_unique<OpenGLRenderBackend>(*presentationPort_, options_, functions_, contextLease_));
            }

        private:
            IOpenGLPresentationPort* presentationPort_{nullptr};
            OpenGLBackendOptions options_{};
            Detail::OpenGLCommandFunctions functions_{};
            std::shared_ptr<OpenGLContextLease> contextLease_;
        };
    } // namespace

    namespace Detail
    {
        Result<void> RegisterOpenGLRenderBackendWithFunctions(RenderBackendRegistry& registry,
                                                              IOpenGLPresentationPort& presentationPort,
                                                              const OpenGLBackendOptions options,
                                                              const OpenGLCommandFunctions functions)
        {
            if (!functions.IsValid() || options.majorVersion == 0)
            {
                return Result<void>::Failure(
                    MakeOpenGLError(OpenGLBackendErrors::InvalidRegistration,
                                    "OpenGL backend registration options are invalid."));
            }
            return registry.Register(RenderBackendDescriptor{
                .id = RenderBackendId{"opengl"},
                .displayName = "OpenGL",
                .provider = std::make_unique<OpenGLBackendProvider>(presentationPort, options, functions),
            });
        }
    } // namespace Detail

    /** @copydoc GetOpenGLRenderBackendModuleInfo */
    const RenderBackendModuleInfo& GetOpenGLRenderBackendModuleInfo() noexcept
    {
        static const RenderBackendModuleInfo info{
            .id = RenderBackendId{"opengl"},
            .displayName = "OpenGL",
            .windowRequirements =
            RenderHostWindowRequirements{
                .presentation = RenderPresentationKind::OpenGL,
                .resizable = true,
                .highPixelDensity = true,
            },
            .supportsInteractivePresentation = true,
        };
        return info;
    }

    /** @copydoc RegisterOpenGLRenderBackend */
    Result<void> RegisterOpenGLRenderBackend(RenderBackendRegistry& registry, IOpenGLPresentationPort& presentationPort,
                                             const OpenGLBackendOptions options)
    {
        return Detail::RegisterOpenGLRenderBackendWithFunctions(registry, presentationPort, options,
                                                                ProductionFunctions());
    }
} // namespace Horo::Render
