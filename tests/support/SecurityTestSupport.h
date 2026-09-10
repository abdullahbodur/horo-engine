#pragma once

#include "Horo/Security/ArtifactSignature.h"
#include "Horo/Security/SecurityErrors.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace Horo::Tests {
    class AcceptingTestSignatureProvider final : public Security::SignatureProvider {
    public:
        [[nodiscard]] bool Supports(const Security::SignatureAlgorithm algorithm) const noexcept override {
            return algorithm == Security::SignatureAlgorithm::EcdsaP256Sha256;
        }

        [[nodiscard]] Result<void> Verify(Security::SignatureAlgorithm, std::span<const std::byte>, const Sha256Digest &,
                                          std::span<const std::byte>) const override {
            return Result<void>::Success();
        }
    };

    /** @brief Creates a test-only gate that still binds evidence to the exact fixture bytes. */
    [[nodiscard]] inline std::shared_ptr<const Security::NativeArtifactGate> CreateAcceptingArtifactGate() {
        auto roots = std::make_shared<Security::TrustedRootStore>();
        static_cast<void>(roots->Add({.publisherId = "com.horo.tests", .keyId = "fixture", .publicKey = {std::byte{1}}}));
        auto verifier = std::make_shared<Security::ArtifactVerifier>(std::make_shared<AcceptingTestSignatureProvider>(), roots);
        return std::make_shared<Security::DetachedFileArtifactGate>(verifier,
                                                                    [](const std::filesystem::path &path)
                                                                        -> Result<Security::DetachedSignatureEnvelope> {
            std::ifstream input{path, std::ios::binary};
            if (!input)
                return Result<Security::DetachedSignatureEnvelope>::Failure(MakeError(SecurityErrors::MissingEvidence));
            const std::string bytes{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
            return Result<Security::DetachedSignatureEnvelope>::Success(
                {.publisherId = "com.horo.tests",
                 .keyId = "fixture",
                 .artifactDigest = ComputeSha256({reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()}),
                 .signature = {std::byte{1}}});
        });
    }
}  // namespace Horo::Tests
