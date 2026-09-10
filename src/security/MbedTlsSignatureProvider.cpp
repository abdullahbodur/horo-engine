#include "Horo/Security/ArtifactSignature.h"
#include "Horo/Security/SecurityErrors.h"

#include <algorithm>
#include <array>
#include <mbedtls/bignum.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>

namespace Horo::Security {
    namespace {
        [[nodiscard]] int VerifyEcdsaP256(const std::span<const std::byte> publicKey, const Sha256Digest &payloadDigest,
                                          const std::span<const std::byte> signature) {
            mbedtls_ecp_group group;
            mbedtls_ecp_point point;
            mbedtls_mpi r;
            mbedtls_mpi s;
            mbedtls_ecp_group_init(&group);
            mbedtls_ecp_point_init(&point);
            mbedtls_mpi_init(&r);
            mbedtls_mpi_init(&s);
            std::array<unsigned char, 65> keyBytes{};
            std::array<unsigned char, 64> signatureBytes{};
            std::ranges::transform(publicKey, keyBytes.begin(), [](const std::byte byte) {
                return std::to_integer<unsigned char>(byte);
            });
            std::ranges::transform(signature, signatureBytes.begin(), [](const std::byte byte) {
                return std::to_integer<unsigned char>(byte);
            });
            int status = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
            if (status == 0)
                status = mbedtls_ecp_point_read_binary(&group, &point, keyBytes.data(), keyBytes.size());
            if (status == 0)
                status = mbedtls_ecp_check_pubkey(&group, &point);
            if (status == 0)
                status = mbedtls_mpi_read_binary(&r, signatureBytes.data(), 32U);
            if (status == 0)
                status = mbedtls_mpi_read_binary(&s, signatureBytes.data() + 32U, 32U);
            if (status == 0)
                status = mbedtls_ecdsa_verify(&group, payloadDigest.bytes.data(), payloadDigest.bytes.size(), &point, &r, &s);
            mbedtls_mpi_free(&s);
            mbedtls_mpi_free(&r);
            mbedtls_ecp_point_free(&point);
            mbedtls_ecp_group_free(&group);
            return status;
        }

        class MbedTlsSignatureProvider final : public SignatureProvider {
        public:
            [[nodiscard]] bool Supports(const SignatureAlgorithm algorithm) const noexcept override {
                return algorithm == SignatureAlgorithm::EcdsaP256Sha256;
            }

            [[nodiscard]] Result<void> Verify(const SignatureAlgorithm algorithm, const std::span<const std::byte> publicKey,
                                              const Sha256Digest &payloadDigest,
                                              const std::span<const std::byte> signature) const override {
                if (!Supports(algorithm))
                    return Result<void>::Failure(MakeError(SecurityErrors::UnsupportedAlgorithm));
                if (publicKey.size() != 65U || publicKey.front() != std::byte{0x04} || signature.size() != 64U)
                    return Result<void>::Failure(MakeError(SecurityErrors::InvalidSignature));
                const int status = VerifyEcdsaP256(publicKey, payloadDigest, signature);
                return status == 0 ? Result<void>::Success() : Result<void>::Failure(MakeError(SecurityErrors::InvalidSignature));
            }
        };
    }  // namespace

    /** @copydoc CreateMbedTlsSignatureProvider */
    std::shared_ptr<const SignatureProvider> CreateMbedTlsSignatureProvider() {
        return std::make_shared<const MbedTlsSignatureProvider>();
    }
}  // namespace Horo::Security
