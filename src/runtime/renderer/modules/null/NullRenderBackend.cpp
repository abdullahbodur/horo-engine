#include "Horo/Runtime/Render/NullBackendModule.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace Horo::Render
{
    namespace
    {
        /** @brief Creates one backend-domain typed error. */
        [[nodiscard]] Error MakeBackendError(std::string code, std::string message)
        {
            return Error{
                ErrorCode{std::move(code)}, ErrorDomainId{"horo.render"}, ErrorSeverity::Error, std::move(message), {}
            };
        }

        /** @brief Headless backend that validates renderer lifecycle without acquiring GPU resources. */
        class NullRenderBackend final : public IRenderBackend
        {
        public:
            ~NullRenderBackend() override
            {
                Shutdown();
            }

            /** @copydoc IRenderBackend::Initialize */
            Result<void> Initialize(const RenderBackendConfig& config) override
            {
                if (initialized_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.already_initialized",
                                         "Renderer backend is already initialized."));
                }
                if (config.requirePresentation)
                {
                    return Result<void>::Failure(MakeBackendError("render.null.presentation_unsupported",
                                                                  "Null renderer cannot satisfy a presentation requirement."));
                }
                if (!config.IsValid())
                {
                    return Result<void>::Failure(MakeBackendError("render.backend.invalid_config",
                                                                  "Frames in flight must be in the inclusive range [1, 8]."));
                }

                config_ = config;
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
                        MakeBackendError("render.backend.not_initialized", "Renderer backend is not initialized."));
                }
                if (frameActive_)
                {
                    return Result<FrameToken>::Failure(
                        MakeBackendError("render.backend.frame_already_active", "A renderer frame is already active."));
                }
                if (descriptor.frameNumber == 0 || !descriptor.outputExtent.IsValid())
                {
                    return Result<FrameToken>::Failure(MakeBackendError("render.backend.invalid_frame_descriptor",
                                                                        "Frame number and output extent must be valid."));
                }
                if (nextFrameToken_ == std::numeric_limits<std::uint64_t>::max())
                {
                    return Result<FrameToken>::Failure(
                        MakeBackendError("render.backend.frame_token_exhausted", "Frame token space is exhausted."));
                }

                frameActive_ = true;
                activeFrame_ = FrameToken{nextFrameToken_++};
                return Result<FrameToken>::Success(activeFrame_);
            }

            /** @copydoc IRenderBackend::Execute */
            Result<void> Execute(const RenderExecutionPlan& plan) override
            {
                if (!initialized_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.not_initialized", "Renderer backend is not initialized."));
                }
                if (!frameActive_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.no_active_frame", "No renderer frame is active."));
                }
                if (plan.frame != activeFrame_)
                {
                    return Result<void>::Failure(MakeBackendError("render.backend.frame_token_mismatch",
                                                                  "Execution plan does not match the active frame."));
                }

                for (std::size_t index = 0; index < plan.orderedPasses.size(); ++index)
                {
                    const RenderPassDescriptor& pass = plan.orderedPasses[index];
                    if (!pass.id.IsValid())
                    {
                        return Result<void>::Failure(MakeBackendError("render.backend.invalid_execution_plan",
                                                                      "Execution plan contains an invalid render pass ID."));
                    }
                    switch (pass.kind)
                    {
                    case RenderPassKind::Graphics:
                    case RenderPassKind::Copy:
                        break;
                    case RenderPassKind::Compute:
                        if (!capabilities_.supportsCompute)
                        {
                            return Result<void>::Failure(MakeBackendError("render.backend.unsupported_pass_kind",
                                                                          "Execution plan requires unsupported compute work."));
                        }
                        break;
                    default:
                        return Result<void>::Failure(MakeBackendError("render.backend.invalid_execution_plan",
                                                                      "Execution plan contains an invalid render pass kind."));
                    }
                    if (pass.primaryOutput.has_value())
                    {
                        if (pass.kind != RenderPassKind::Graphics)
                        {
                            return Result<void>::Failure(
                                MakeBackendError("render.backend.invalid_execution_plan",
                                                 "Only graphics passes may bind the primary output attachment."));
                        }

                        if (!pass.primaryOutput->IsValid())
                        {
                            return Result<void>::Failure(MakeBackendError("render.backend.invalid_execution_plan",
                                                                          "Primary output attachment operations are invalid."));
                        }
                    }
                    for (std::size_t previous = 0; previous < index; ++previous)
                    {
                        if (pass.id == plan.orderedPasses[previous].id)
                        {
                            return Result<void>::Failure(MakeBackendError(
                                "render.backend.invalid_execution_plan",
                                "Execution plan contains duplicate render pass IDs."));
                        }
                    }
                }

                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::Present */
            Result<void> Present(FrameToken frame) override
            {
                if (!initialized_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.not_initialized", "Renderer backend is not initialized."));
                }
                if (!frameActive_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.no_active_frame", "No renderer frame is active."));
                }
                if (frame != activeFrame_)
                {
                    return Result<void>::Failure(MakeBackendError("render.backend.frame_token_mismatch",
                                                                  "Frame token does not match the active frame."));
                }

                frameActive_ = false;
                activeFrame_ = {};
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
            Result<void> Resize(FramebufferExtent extent) override
            {
                if (!initialized_)
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.not_initialized", "Renderer backend is not initialized."));
                }
                if (!extent.IsValid())
                {
                    return Result<void>::Failure(
                        MakeBackendError("render.backend.invalid_extent", "Renderer output extent must be non-zero."));
                }
                if (frameActive_)
                {
                    return Result<void>::Failure(MakeBackendError("render.backend.frame_active",
                                                                  "Renderer output cannot resize while a frame is active."));
                }

                extent_ = extent;
                return Result<void>::Success();
            }

            /** @copydoc IRenderBackend::Shutdown */
            void Shutdown() noexcept override
            {
                frameActive_ = false;
                initialized_ = false;
                activeFrame_ = {};
                extent_ = {};
                config_ = {};
            }

        private:
            RenderBackendCapabilities capabilities_{.backend = RenderBackendId{"null"}};
            RenderBackendConfig config_{};
            FramebufferExtent extent_{};
            FrameToken activeFrame_{};
            std::uint64_t nextFrameToken_{1};
            bool initialized_{false};
            bool frameActive_{false};
        };

        /** @brief Provides inert null renderer instances without process side effects. */
        class NullBackendProvider final : public IRenderBackendProvider
        {
        public:
            Result<std::unique_ptr<IRenderBackend>> Create() const override
            {
                return Result<std::unique_ptr<IRenderBackend>>::Success(std::make_unique<NullRenderBackend>());
            }
        };
    } // namespace

    /** @copydoc RegisterNullRenderBackend */
    Result<void> RegisterNullRenderBackend(RenderBackendRegistry& registry)
    {
        return registry.Register(RenderBackendDescriptor{
            .id = RenderBackendId{"null"},
            .displayName = "Null",
            .provider = std::make_unique<NullBackendProvider>(),
        });
    }
} // namespace Horo::Render
