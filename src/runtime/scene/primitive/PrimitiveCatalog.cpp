#include "Horo/Runtime/Scene/PrimitiveCatalog.h"

#include <algorithm>
#include <array>

namespace Horo::Runtime
{
namespace
{
constexpr std::array kCorePrimitives{
    PrimitiveDescriptor{.id = {"primitive.mesh.box"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Cube / Box",
                        .defaultObjectName = "Box",
                        .iconToken = "primitive.box",
                        .isRenderable = true,
                        .isPhysicsSolidByDefault = true,
                        .meshType = PrimitiveMeshType::Box,
                        .defaultCollider = ColliderShapeType::Box},
    PrimitiveDescriptor{.id = {"primitive.mesh.sphere"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Sphere",
                        .defaultObjectName = "Sphere",
                        .iconToken = "primitive.sphere",
                        .isRenderable = true,
                        .isPhysicsSolidByDefault = true,
                        .meshType = PrimitiveMeshType::Sphere,
                        .defaultCollider = ColliderShapeType::Sphere},
    PrimitiveDescriptor{.id = {"primitive.mesh.capsule"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Capsule",
                        .defaultObjectName = "Capsule",
                        .iconToken = "primitive.capsule",
                        .isRenderable = true,
                        .isPhysicsSolidByDefault = true,
                        .meshType = PrimitiveMeshType::Capsule,
                        .defaultCollider = ColliderShapeType::Capsule},
    PrimitiveDescriptor{.id = {"primitive.mesh.cylinder"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Cylinder",
                        .defaultObjectName = "Cylinder",
                        .iconToken = "primitive.cylinder",
                        .isRenderable = true,
                        .meshType = PrimitiveMeshType::Cylinder},
    PrimitiveDescriptor{.id = {"primitive.mesh.cone"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Cone",
                        .defaultObjectName = "Cone",
                        .iconToken = "primitive.cone",
                        .isRenderable = true,
                        .meshType = PrimitiveMeshType::Cone},
    PrimitiveDescriptor{.id = {"primitive.mesh.plane"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Plane",
                        .defaultObjectName = "Plane",
                        .iconToken = "primitive.plane",
                        .isRenderable = true,
                        .meshType = PrimitiveMeshType::Plane,
                        .defaultCollider = ColliderShapeType::StaticPlane},
    PrimitiveDescriptor{.id = {"primitive.mesh.quad"},
                        .category = PrimitiveCategory::Mesh,
                        .creationGroup = PrimitiveCreationGroup::Objects3D,
                        .displayName = "Quad",
                        .defaultObjectName = "Quad",
                        .iconToken = "primitive.quad",
                        .isRenderable = true,
                        .meshType = PrimitiveMeshType::Quad},
    PrimitiveDescriptor{.id = {"primitive.collider.box"},
                        .category = PrimitiveCategory::Collider,
                        .displayName = "Box Collider",
                        .defaultObjectName = "Box Collider",
                        .iconToken = "primitive.collider.box",
                        .isPhysicsSolidByDefault = true},
    PrimitiveDescriptor{.id = {"primitive.collider.sphere"},
                        .category = PrimitiveCategory::Collider,
                        .displayName = "Sphere Collider",
                        .defaultObjectName = "Sphere Collider",
                        .iconToken = "primitive.collider.sphere",
                        .isPhysicsSolidByDefault = true},
    PrimitiveDescriptor{.id = {"primitive.collider.capsule"},
                        .category = PrimitiveCategory::Collider,
                        .displayName = "Capsule Collider",
                        .defaultObjectName = "Capsule Collider",
                        .iconToken = "primitive.collider.capsule",
                        .isPhysicsSolidByDefault = true},
    PrimitiveDescriptor{.id = {"primitive.collider.static_plane"},
                        .category = PrimitiveCategory::Collider,
                        .displayName = "Static Plane Collider",
                        .defaultObjectName = "Static Plane Collider",
                        .iconToken = "primitive.collider.plane"},
    PrimitiveDescriptor{.id = {"primitive.object.empty"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Root,
                        .displayName = "Empty Object",
                        .defaultObjectName = "Empty Object",
                        .iconToken = "primitive.empty",
                        .sceneObjectType = SceneObjectPrimitiveType::Empty},
    PrimitiveDescriptor{.id = {"primitive.object.camera"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Cameras,
                        .displayName = "Camera",
                        .defaultObjectName = "Camera",
                        .iconToken = "primitive.camera",
                        .sceneObjectType = SceneObjectPrimitiveType::Camera},
    PrimitiveDescriptor{.id = {"primitive.object.light_directional"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Lights,
                        .displayName = "Directional Light",
                        .defaultObjectName = "Directional Light",
                        .iconToken = "primitive.light.directional",
                        .sceneObjectType = SceneObjectPrimitiveType::DirectionalLight},
    PrimitiveDescriptor{.id = {"primitive.object.light_point"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Lights,
                        .displayName = "Point Light",
                        .defaultObjectName = "Point Light",
                        .iconToken = "primitive.light.point",
                        .sceneObjectType = SceneObjectPrimitiveType::PointLight},
    PrimitiveDescriptor{.id = {"primitive.object.light_spot"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Lights,
                        .displayName = "Spot Light",
                        .defaultObjectName = "Spot Light",
                        .iconToken = "primitive.light.spot",
                        .sceneObjectType = SceneObjectPrimitiveType::SpotLight},
    PrimitiveDescriptor{.id = {"primitive.object.trigger_volume"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Volumes,
                        .displayName = "Trigger Volume",
                        .defaultObjectName = "Trigger Volume",
                        .iconToken = "primitive.trigger_volume",
                        .sceneObjectType = SceneObjectPrimitiveType::TriggerVolume},
    PrimitiveDescriptor{.id = {"primitive.object.audio_source"},
                        .category = PrimitiveCategory::SceneObject,
                        .creationGroup = PrimitiveCreationGroup::Audio,
                        .displayName = "Audio Source",
                        .defaultObjectName = "Audio Source",
                        .iconToken = "primitive.audio_source",
                        .sceneObjectType = SceneObjectPrimitiveType::AudioSource},
};
} // namespace

/** @copydoc PrimitiveCatalog::All */
std::span<const PrimitiveDescriptor> PrimitiveCatalog::All() noexcept
{
    return kCorePrimitives;
}

/** @copydoc PrimitiveCatalog::Find(std::string_view) */
const PrimitiveDescriptor *PrimitiveCatalog::Find(const std::string_view id) noexcept
{
    const auto found = std::ranges::find_if(kCorePrimitives,
                                            [id](const PrimitiveDescriptor &descriptor) {
                                                return descriptor.id.value == id;
                                            });
    return found == kCorePrimitives.end() ? nullptr : &*found;
}

/** @copydoc PrimitiveCatalog::Find(PrimitiveMeshType) */
const PrimitiveDescriptor *PrimitiveCatalog::Find(const PrimitiveMeshType meshType) noexcept
{
    const auto found = std::ranges::find_if(kCorePrimitives, [meshType](const PrimitiveDescriptor &descriptor) {
        return descriptor.meshType == meshType;
    });
    return found == kCorePrimitives.end() ? nullptr : &*found;
}
} // namespace Horo::Runtime
