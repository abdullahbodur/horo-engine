#include "Horo/Animation/SkeletalMeshSkinning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ranges>

namespace Horo::Animation {
    namespace {
        /** @brief Returns one stable skinning failure without exposing validation implementation state. */
        template <typename Value> [[nodiscard]] Result<Value> Fail(const ErrorCodeDescriptor &error) {
            return Result<Value>::Failure(MakeError(error));
        }

        /** @brief Checks caller limits that must be non-zero. */
        [[nodiscard]] constexpr bool HasNonZeroLimits(const SkeletalMeshSkinningLimits &limits) noexcept {
            return limits.maximumLods > 0 && limits.maximumVerticesPerLod > 0 && limits.maximumSectionsPerLod > 0 &&
                   limits.maximumBindingJoints > 0 && limits.maximumPaletteJoints > 0 && limits.maximumInfluencesPerVertex > 0;
        }

        /** @brief Checks caller limits against immutable engine safety ceilings. */
        [[nodiscard]] constexpr bool FitsHardLimits(const SkeletalMeshSkinningLimits &limits) noexcept {
            return limits.maximumLods <= SkeletalMeshSkinningHardLimits::Lods &&
                   limits.maximumVerticesPerLod <= SkeletalMeshSkinningHardLimits::VerticesPerLod &&
                   limits.maximumSectionsPerLod <= SkeletalMeshSkinningHardLimits::SectionsPerLod &&
                   limits.maximumBindingJoints <= SkeletalMeshSkinningHardLimits::BindingJoints &&
                   limits.maximumPaletteJoints <= SkeletalMeshSkinningHardLimits::PaletteJoints &&
                   limits.maximumInfluencesPerVertex <= SkeletalMeshSkinningHardLimits::InfluencesPerVertex;
        }

        /** @brief Checks that an optional reload identity matches the candidate mesh. */
        [[nodiscard]] bool MatchesReload(const SkeletalMeshSkinningData &candidate,
                                         const SkeletalMeshSkinningBuildContext &context) noexcept {
            if (!context.replacing.has_value())
                return true;
            return context.replacing->IsValid() && *context.replacing == candidate.mesh;
        }

        /** @brief Validates owner admission before inspecting candidate data. */
        [[nodiscard]] Result<void> ValidateAdmission(const SkeletalMeshSkinningAdmissionState admission) {
            if (admission == SkeletalMeshSkinningAdmissionState::CancellationRequested)
                return Fail<void>(AnimationErrors::SkinningValidationCancelled);
            if (admission != SkeletalMeshSkinningAdmissionState::Accepting)
                return Fail<void>(AnimationErrors::SkinningAdmissionRejected);
            return Result<void>::Success();
        }

        /** @brief Validates captured policy and candidate aggregate counts. */
        [[nodiscard]] Result<void> ValidateLimits(const SkeletalMeshSkinningData &candidate, const SkeletalMeshSkinningLimits &limits) {
            if (!HasNonZeroLimits(limits) || !FitsHardLimits(limits))
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            if (candidate.lods.empty() || candidate.lods.size() > limits.maximumLods)
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            return Result<void>::Success();
        }

        /** @brief Validates lifecycle, top-level identity, version, and operation limits. */
        [[nodiscard]] Result<void> ValidateBuildRequest(const SkeletalMeshSkinningData &candidate,
                                                        const SkeletalMeshSkinningBuildContext &context) {
            if (auto validation = ValidateAdmission(context.admission); validation.HasError())
                return validation;
            if (candidate.contractVersion != CurrentSkeletalMeshSkinningContractVersion)
                return Fail<void>(AnimationErrors::SkinningVersionUnsupported);
            if (!candidate.mesh.IsValid())
                return Fail<void>(AnimationErrors::IdentityInvalid);
            if (!MatchesReload(candidate, context))
                return Fail<void>(AnimationErrors::SkinningReloadMismatch);
            return ValidateLimits(candidate, context.limits);
        }

        /** @brief Finds a stable joint in the target skeleton without assuming hierarchy-array identity. */
        [[nodiscard]] bool ContainsSkeletonJoint(const SkeletonAsset &skeleton, const JointId joint) noexcept {
            return std::ranges::find(skeleton.Data().joints, joint, &SkeletonJoint::id) != skeleton.Data().joints.end();
        }

        /** @brief Validates exact skeleton identity, schema, and publication generation. */
        [[nodiscard]] Result<void> ValidateSkeletonCompatibility(const SkeletonBinding &binding, const SkeletonAsset &skeleton,
                                                                 const SkeletonAssetGeneration currentGeneration) {
            if (!binding.skeleton.IsValid() || binding.skeleton != skeleton.Data().skeleton ||
                binding.skeletonContractVersion != skeleton.Data().contractVersion)
                return Fail<void>(AnimationErrors::SkinningSkeletonMismatch);
            if (!binding.skeletonGeneration.IsValid() || !currentGeneration.IsValid() || binding.skeletonGeneration != currentGeneration)
                return Fail<void>(AnimationErrors::SkinningBindingStale);
            return Result<void>::Success();
        }

        /** @brief Checks whether a sorted stable-ID range contains one identity. */
        [[nodiscard]] bool ContainsMeshJoint(const std::vector<SkeletonJointRemap> &remap, const SkinningJointId joint) {
            const auto found = std::ranges::lower_bound(remap, joint, {}, &SkeletonJointRemap::meshJoint);
            return found != remap.end() && found->meshJoint == joint;
        }

        /** @brief Validates and canonicalizes the complete one-to-one joint remap. */
        [[nodiscard]] Result<void> ValidateRemap(std::vector<SkeletonJointRemap> &remap, const SkeletonAsset &skeleton,
                                                 const SkeletalMeshSkinningLimits &limits) {
            if (remap.empty() || remap.size() > limits.maximumBindingJoints)
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            std::ranges::sort(remap, {}, &SkeletonJointRemap::meshJoint);
            std::vector<JointId> targets;
            targets.reserve(remap.size());
            for (const SkeletonJointRemap &entry : remap) {
                if (!entry.meshJoint.IsValid() || !entry.skeletonJoint.IsValid())
                    return Fail<void>(AnimationErrors::IdentityInvalid);
                if (!ContainsSkeletonJoint(skeleton, entry.skeletonJoint))
                    return Fail<void>(AnimationErrors::SkinningJointMissing);
                targets.push_back(entry.skeletonJoint);
            }
            if (std::ranges::adjacent_find(remap, {}, &SkeletonJointRemap::meshJoint) != remap.end())
                return Fail<void>(AnimationErrors::SkinningDuplicateIdentity);
            std::ranges::sort(targets);
            if (std::ranges::adjacent_find(targets) != targets.end())
                return Fail<void>(AnimationErrors::SkinningDuplicateIdentity);
            return Result<void>::Success();
        }

        /** @brief Validates and canonicalizes one section palette. */
        [[nodiscard]] Result<void> ValidatePalette(std::vector<SkinningJointId> &palette, const std::vector<SkeletonJointRemap> &remap,
                                                   const SkeletalMeshSkinningLimits &limits) {
            if (palette.empty() || palette.size() > limits.maximumPaletteJoints)
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            std::ranges::sort(palette);
            for (const SkinningJointId joint : palette) {
                if (!joint.IsValid())
                    return Fail<void>(AnimationErrors::IdentityInvalid);
                if (!ContainsMeshJoint(remap, joint))
                    return Fail<void>(AnimationErrors::SkinningJointMissing);
            }
            if (std::ranges::adjacent_find(palette) != palette.end())
                return Fail<void>(AnimationErrors::SkinningDuplicateIdentity);
            return Result<void>::Success();
        }

        /** @brief Checks one influence before normalization. */
        [[nodiscard]] bool IsInfluenceRepresentable(const SkinningInfluence &influence) noexcept {
            return influence.joint.IsValid() && std::isfinite(influence.weight) && influence.weight > 0.0F;
        }

        /** @brief Validates canonical joint membership and accumulates one finite weight sum. */
        [[nodiscard]] Result<double> ValidateInfluences(const std::span<const SkinningInfluence> influences,
                                                        const std::span<const SkinningJointId> palette) {
            double totalWeight = 0.0;
            for (const SkinningInfluence &influence : influences) {
                if (!IsInfluenceRepresentable(influence))
                    return Fail<double>(AnimationErrors::SkinningInfluenceInvalid);
                if (!std::ranges::binary_search(palette, influence.joint))
                    return Fail<double>(AnimationErrors::SkinningJointMissing);
                totalWeight += static_cast<double>(influence.weight);
            }
            if (!std::isfinite(totalWeight) || totalWeight <= 0.0)
                return Fail<double>(AnimationErrors::SkinningInfluenceInvalid);
            return Result<double>::Success(totalWeight);
        }

        /** @brief Validates, normalizes, and deterministically orders one vertex influence set. */
        [[nodiscard]] Result<void> NormalizeVertex(SkinnedVertex &vertex, const std::span<const SkinningJointId> palette,
                                                   const SkeletalMeshSkinningLimits &limits) {
            if (vertex.influences.empty() || vertex.influences.size() > limits.maximumInfluencesPerVertex)
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            std::ranges::sort(vertex.influences, {}, &SkinningInfluence::joint);
            if (std::ranges::adjacent_find(vertex.influences, {}, &SkinningInfluence::joint) != vertex.influences.end())
                return Fail<void>(AnimationErrors::SkinningInfluenceInvalid);
            const auto validatedWeight = ValidateInfluences(vertex.influences, palette);
            if (validatedWeight.HasError())
                return Result<void>::Failure(validatedWeight.ErrorValue());
            const double totalWeight = validatedWeight.Value();
            for (SkinningInfluence &influence : vertex.influences)
                influence.weight = static_cast<float>(static_cast<double>(influence.weight) / totalWeight);
            std::ranges::sort(vertex.influences, [](const SkinningInfluence &left, const SkinningInfluence &right) {
                if (left.weight != right.weight)
                    return left.weight > right.weight;
                return left.joint < right.joint;
            });
            return Result<void>::Success();
        }

        /** @brief Validates one section and canonicalizes every vertex in its range. */
        [[nodiscard]] Result<std::uint64_t> ValidateSection(SkeletalMeshSection &section, std::vector<SkinnedVertex> &vertices,
                                                            const std::vector<SkeletonJointRemap> &remap,
                                                            const SkeletalMeshSkinningLimits &limits, const std::uint64_t expectedFirst) {
            if (!section.id.IsValid())
                return Fail<std::uint64_t>(AnimationErrors::IdentityInvalid);
            const std::uint64_t end = static_cast<std::uint64_t>(section.firstVertex) + section.vertexCount;
            if (section.vertexCount == 0 || static_cast<std::uint64_t>(section.firstVertex) != expectedFirst || end > vertices.size())
                return Fail<std::uint64_t>(AnimationErrors::SkinningLayoutInvalid);
            if (auto validation = ValidatePalette(section.palette, remap, limits); validation.HasError())
                return Result<std::uint64_t>::Failure(validation.ErrorValue());
            for (std::uint64_t vertexIndex = section.firstVertex; vertexIndex < end; ++vertexIndex) {
                if (auto validation = NormalizeVertex(vertices[static_cast<std::size_t>(vertexIndex)], section.palette, limits);
                    validation.HasError())
                    return Result<std::uint64_t>::Failure(validation.ErrorValue());
            }
            return Result<std::uint64_t>::Success(end);
        }

        /** @brief Canonicalizes section order and validates an exact vertex partition. */
        [[nodiscard]] Result<void> ValidateSections(std::vector<SkeletalMeshSection> &sections, std::vector<SkinnedVertex> &vertices,
                                                    const std::vector<SkeletonJointRemap> &remap,
                                                    const SkeletalMeshSkinningLimits &limits) {
            if (sections.empty() || sections.size() > limits.maximumSectionsPerLod)
                return Fail<void>(AnimationErrors::SkinningLimitExceeded);
            std::ranges::sort(sections, [](const SkeletalMeshSection &left, const SkeletalMeshSection &right) {
                if (left.firstVertex != right.firstVertex)
                    return left.firstVertex < right.firstVertex;
                return left.id < right.id;
            });
            std::vector<SkeletalMeshSectionId> sectionIds;
            sectionIds.reserve(sections.size());
            std::uint64_t expectedFirst = 0;
            for (SkeletalMeshSection &section : sections) {
                auto validatedEnd = ValidateSection(section, vertices, remap, limits, expectedFirst);
                if (validatedEnd.HasError())
                    return Result<void>::Failure(validatedEnd.ErrorValue());
                expectedFirst = validatedEnd.Value();
                sectionIds.push_back(section.id);
            }
            std::ranges::sort(sectionIds);
            if (std::ranges::adjacent_find(sectionIds) != sectionIds.end())
                return Fail<void>(AnimationErrors::SkinningDuplicateIdentity);
            if (expectedFirst != vertices.size())
                return Fail<void>(AnimationErrors::SkinningLayoutInvalid);
            return Result<void>::Success();
        }

        /** @brief Validates and canonicalizes every bounded LOD. */
        [[nodiscard]] Result<void> ValidateLods(std::vector<SkeletalMeshSkinningLod> &lods, const std::vector<SkeletonJointRemap> &remap,
                                                const SkeletalMeshSkinningLimits &limits) {
            std::ranges::sort(lods, {}, &SkeletalMeshSkinningLod::level);
            for (std::size_t index = 0; index < lods.size(); ++index) {
                SkeletalMeshSkinningLod &lod = lods[index];
                if (static_cast<std::size_t>(lod.level) != index || !lod.localBounds.IsValid())
                    return Fail<void>(AnimationErrors::SkinningLayoutInvalid);
                if (lod.vertices.empty() || lod.vertices.size() > limits.maximumVerticesPerLod)
                    return Fail<void>(AnimationErrors::SkinningLimitExceeded);
                if (auto validation = ValidateSections(lod.sections, lod.vertices, remap, limits); validation.HasError())
                    return validation;
            }
            return Result<void>::Success();
        }
    }  // namespace

    /** @copydoc SkeletalMeshSkinningAsset::Create */
    Result<SkeletalMeshSkinningAsset> SkeletalMeshSkinningAsset::Create(SkeletalMeshSkinningData candidate, const SkeletonAsset &skeleton,
                                                                        const SkeletalMeshSkinningBuildContext &context) {
        if (auto validation = ValidateBuildRequest(candidate, context); validation.HasError())
            return Result<SkeletalMeshSkinningAsset>::Failure(validation.ErrorValue());
        if (auto validation = ValidateSkeletonCompatibility(candidate.binding, skeleton, context.currentSkeletonGeneration);
            validation.HasError())
            return Result<SkeletalMeshSkinningAsset>::Failure(validation.ErrorValue());
        if (auto validation = ValidateRemap(candidate.binding.jointRemap, skeleton, context.limits); validation.HasError())
            return Result<SkeletalMeshSkinningAsset>::Failure(validation.ErrorValue());
        if (auto validation = ValidateLods(candidate.lods, candidate.binding.jointRemap, context.limits); validation.HasError())
            return Result<SkeletalMeshSkinningAsset>::Failure(validation.ErrorValue());
        return Result<SkeletalMeshSkinningAsset>::Success(SkeletalMeshSkinningAsset{std::move(candidate)});
    }

    /** @copydoc SkeletalMeshSkinningAsset::Data */
    const SkeletalMeshSkinningData &SkeletalMeshSkinningAsset::Data() const noexcept {
        return data_;
    }

    /** @copydoc SkeletalMeshSkinningAsset::Palette */
    std::span<const SkinningJointId> SkeletalMeshSkinningAsset::Palette(const std::uint16_t lodLevel,
                                                                        const SkeletalMeshSectionId section) const noexcept {
        if (static_cast<std::size_t>(lodLevel) >= data_.lods.size() || !section.IsValid())
            return {};
        const auto found = std::ranges::find(data_.lods[lodLevel].sections, section, &SkeletalMeshSection::id);
        if (found == data_.lods[lodLevel].sections.end())
            return {};
        return found->palette;
    }
}  // namespace Horo::Animation
