#include "renderer/Skeleton.h"

#include <cassert>
#include <string_view>
#include <utility>

namespace Horo {
    void Skeleton::AddBone(Bone bone) { m_bones.push_back(std::move(bone)); }

    int Skeleton::BoneCount() const { return static_cast<int>(m_bones.size()); }

    const Bone &Skeleton::GetBone(int index) const {
        assert(index >= 0 && index < static_cast<int>(m_bones.size()));
        return m_bones[static_cast<size_t>(index)];
    }

    int Skeleton::FindBone(const std::string_view name) const {
        for (int i = 0; i < static_cast<int>(m_bones.size()); ++i) {
            if (m_bones[static_cast<size_t>(i)].name == name) {
                return i;
            }
        }
        return -1;
    }

    bool Skeleton::IsValid() const { return !m_bones.empty(); }
} // namespace Horo
