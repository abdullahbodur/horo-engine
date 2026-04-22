#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "math/Mat4.h"

namespace Monolith {
    // Bone — a single joint in the skeleton hierarchy.
    //
    // parentIndex == -1 indicates a root bone (no parent).
    // inverseBindMatrix transforms a vertex from world/model space into the bone's
    // local space as recorded at bind pose.  It is applied before the animated
    // bone transform during skinning:
    //   finalTransform[i] = boneGlobalTransform[i] * inverseBindMatrix[i]
    struct Bone {
        std::string name;
        int parentIndex; // -1 for root bones
        Mat4 inverseBindMatrix; // world-space → bone local space at bind time
    };

    // Skeleton — an indexed, named hierarchy of Bone records.
    //
    // Bones are stored in a flat array; hierarchy is encoded via parentIndex.
    // The array order implies a topological sort guarantee: every bone's parent
    // has a lower index than the bone itself (i.e., roots come first).  This
    // property is relied upon by any code that propagates transforms top-down.
    class Skeleton {
    public:
        Skeleton() = default;

        // Append a bone to the end of the bone list.
        // The caller is responsible for ensuring parentIndex refers to an already-
        // added bone (or is -1).
        void AddBone(Bone bone);

        int BoneCount() const;

        // Returns a const reference to the bone at `index`.
        // Asserts/undefined if index is out of range.
        const Bone &GetBone(int index) const;

        // Linear search by name.  Returns -1 if no bone with that name exists.
        int FindBone(std::string_view name) const;

        // Returns true when at least one bone has been added.
        bool IsValid() const;

    private:
        std::vector<Bone> m_bones;
    };
} // namespace Monolith
