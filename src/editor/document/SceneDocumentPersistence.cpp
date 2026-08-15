#include "editor/document/SceneDocumentPersistence.h"

#include "Horo/Foundation/Sha256.h"
#include "Horo/Runtime/Scene/PrimitiveCatalog.h"

#include <chrono>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <system_error>
#include <variant>

namespace Horo::Editor {
    namespace {
        using Json = nlohmann::json;

        constexpr std::uint32_t kSceneSchemaVersion = 1;
        constexpr std::uintmax_t kMaximumProjectMetadataBytes = 64U * 1024U;
        constexpr std::uintmax_t kMaximumSceneBytes = 16U * 1024U * 1024U;
        constexpr std::uintmax_t kMaximumRecoveryBytes = 20U * 1024U * 1024U;
        constexpr std::size_t kMaximumSceneObjects = 100'000;

        const ErrorDomainId ScenePersistenceDomain{"horo.editor.scene_persistence"};
        const ErrorCodeDescriptor ScenePathInvalid{
            .domain = ScenePersistenceDomain,
            .code = ErrorCode{"scene_persistence.path_invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The project default scene path is invalid.",
            .remediationHint = "Use a project-relative defaultScene path without parent traversal.",
            .retryable = false,
            .userActionable = true,
        };
        const ErrorCodeDescriptor SceneReadFailed{
            .domain = ScenePersistenceDomain,
            .code = ErrorCode{"scene_persistence.read_failed"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The scene document could not be read.",
            .remediationHint = "Check that the scene is readable and within the supported size limit.",
            .retryable = true,
            .userActionable = true,
        };
        const ErrorCodeDescriptor SceneInvalid{
            .domain = ScenePersistenceDomain,
            .code = ErrorCode{"scene_persistence.invalid"},
            .defaultSeverity = ErrorSeverity::Error,
            .summary = "The scene document is invalid.",
            .remediationHint = "Repair the scene JSON or restore a valid recovery copy.",
            .retryable = false,
            .userActionable = true,
        };

        [[nodiscard]] Error PersistenceError(const ErrorCodeDescriptor &descriptor, std::string message) {
            return MakeError(descriptor, std::move(message));
        }

        [[nodiscard]] Result<std::string> ReadBoundedFile(const std::filesystem::path &absolutePath, const std::uintmax_t maximumBytes) {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(absolutePath, error);
            if (error || size == 0 || size > maximumBytes) {
                return Result<std::string>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to read bounded file '" + absolutePath.string() + "'."));
            }

            std::ifstream input(absolutePath, std::ios::binary);
            if (!input.is_open()) {
                return Result<std::string>::Failure(PersistenceError(SceneReadFailed, "Unable to open '" + absolutePath.string() + "'."));
            }
            std::string contents(size, '\0');
            input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (input.bad() || input.gcount() != static_cast<std::streamsize>(contents.size())) {
                return Result<std::string>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to read all bytes from '" + absolutePath.string() + "'."));
            }
            return Result<std::string>::Success(std::move(contents));
        }

        [[nodiscard]] bool IsSafeProjectRelativePath(const std::filesystem::path &path) {
            if (path.empty() || path.is_absolute() || path.has_root_path())
                return false;
            for (const std::filesystem::path &component : path) {
                if (component == "..")
                    return false;
            }
            return !path.filename().empty();
        }

        [[nodiscard]] bool IsContainedBy(const std::filesystem::path &absoluteRoot, const std::filesystem::path &absoluteCandidate) {
            const std::filesystem::path relative = absoluteCandidate.lexically_relative(absoluteRoot);
            return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
        }

        [[nodiscard]] bool IsResolvedContainedBy(const std::filesystem::path &absoluteRoot,
                                                 const std::filesystem::path &absoluteCandidate) {
            std::error_code error;
            const std::filesystem::path resolvedRoot = std::filesystem::weakly_canonical(absoluteRoot, error);
            if (error)
                return false;
            const std::filesystem::path resolvedCandidate = std::filesystem::weakly_canonical(absoluteCandidate, error);
            return !error && IsContainedBy(resolvedRoot, resolvedCandidate);
        }

        [[nodiscard]] Json Vec2Json(const Math::Vec2 value) {
            return Json::array({value.x, value.y});
        }

        [[nodiscard]] Json Vec3Json(const Math::Vec3 value) {
            return Json::array({value.x, value.y, value.z});
        }

        [[nodiscard]] Json QuaternionJson(const Math::Quaternion value) {
            return Json::array({value.x, value.y, value.z, value.w});
        }

        [[nodiscard]] Json TransformJson(const Math::Transform &value) {
            return Json{
                {"translation", Vec3Json(value.translation)},
                {"rotation", QuaternionJson(value.rotation)},
                {"scale", Vec3Json(value.scale)},
            };
        }

        [[nodiscard]] Result<Math::Vec2> ParseVec2(const Json &value);
        [[nodiscard]] Result<Math::Vec3> ParseVec3(const Json &value);
        [[nodiscard]] Result<Math::Quaternion> ParseQuaternion(const Json &value);

        [[nodiscard]] Json BehaviorFieldValueJson(const Gameplay::BehaviorFieldValue &value) {
            return std::visit([]<typename T>(const T &typed) -> Json {
                if constexpr (std::is_same_v<T, std::monostate>)
                    return {{"type", "null"}, {"value", nullptr}};
                else if constexpr (std::is_same_v<T, bool>)
                    return {{"type", "bool"}, {"value", typed}};
                else if constexpr (std::is_same_v<T, std::int64_t>)
                    return {{"type", "int"}, {"value", typed}};
                else if constexpr (std::is_same_v<T, double>)
                    return {{"type", "number"}, {"value", typed}};
                else if constexpr (std::is_same_v<T, std::string>)
                    return {{"type", "string"}, {"value", typed}};
                else if constexpr (std::is_same_v<T, Math::Vec2>)
                    return {{"type", "vec2"}, {"value", Vec2Json(typed)}};
                else if constexpr (std::is_same_v<T, Math::Vec3>)
                    return {{"type", "vec3"}, {"value", Vec3Json(typed)}};
                else
                    return {{"type", "quaternion"}, {"value", QuaternionJson(typed)}};
            }, value);
        }

        [[nodiscard]] Result<Gameplay::BehaviorFieldValue> ParseBehaviorFieldValue(const Json &value) {
            if (!value.is_object() || !value.contains("type") || !value["type"].is_string() || !value.contains("value"))
                return Result<Gameplay::BehaviorFieldValue>::Failure(PersistenceError(SceneInvalid, "Behavior field is invalid."));
            const std::string type = value["type"].get<std::string>();
            const Json &payload = value["value"];
            if (type == "null" && payload.is_null())
                return Result<Gameplay::BehaviorFieldValue>::Success(std::monostate{});
            if (type == "bool" && payload.is_boolean())
                return Result<Gameplay::BehaviorFieldValue>::Success(payload.get<bool>());
            if (type == "int" && payload.is_number_integer())
                return Result<Gameplay::BehaviorFieldValue>::Success(payload.get<std::int64_t>());
            if (type == "number" && payload.is_number())
                return Result<Gameplay::BehaviorFieldValue>::Success(payload.get<double>());
            if (type == "string" && payload.is_string())
                return Result<Gameplay::BehaviorFieldValue>::Success(payload.get<std::string>());
            if (type == "vec2") {
                auto parsed = ParseVec2(payload);
                if (parsed.HasValue())
                    return Result<Gameplay::BehaviorFieldValue>::Success(parsed.Value());
            }
            if (type == "vec3") {
                auto parsed = ParseVec3(payload);
                if (parsed.HasValue())
                    return Result<Gameplay::BehaviorFieldValue>::Success(parsed.Value());
            }
            if (type == "quaternion") {
                auto parsed = ParseQuaternion(payload);
                if (parsed.HasValue())
                    return Result<Gameplay::BehaviorFieldValue>::Success(parsed.Value());
            }
            return Result<Gameplay::BehaviorFieldValue>::Failure(PersistenceError(SceneInvalid, "Behavior field type is unsupported."));
        }

        template <std::size_t Size> [[nodiscard]] bool IsNumberArray(const Json &value) {
            if (!value.is_array() || value.size() != Size)
                return false;
            return std::ranges::all_of(value, [](const Json &item) {
                return item.is_number();
            });
        }

        [[nodiscard]] Result<Math::Vec2> ParseVec2(const Json &value) {
            if (!IsNumberArray<2>(value))
                return Result<Math::Vec2>::Failure(PersistenceError(SceneInvalid, "Expected a two-component vector."));
            return Result<Math::Vec2>::Success({value[0].get<float>(), value[1].get<float>()});
        }

        [[nodiscard]] Result<Math::Vec3> ParseVec3(const Json &value) {
            if (!IsNumberArray<3>(value))
                return Result<Math::Vec3>::Failure(PersistenceError(SceneInvalid, "Expected a three-component vector."));
            return Result<Math::Vec3>::Success({value[0].get<float>(), value[1].get<float>(), value[2].get<float>()});
        }

        [[nodiscard]] Result<Math::Quaternion> ParseQuaternion(const Json &value) {
            if (!IsNumberArray<4>(value))
                return Result<Math::Quaternion>::Failure(PersistenceError(SceneInvalid, "Expected a quaternion."));
            return Result<Math::Quaternion>::Success(
                {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()});
        }

        [[nodiscard]] Result<Math::Transform> ParseTransform(const Json &value) {
            if (!value.is_object() || !value.contains("translation") || !value.contains("rotation") || !value.contains("scale")) {
                return Result<Math::Transform>::Failure(PersistenceError(SceneInvalid, "Scene transform is incomplete."));
            }
            auto translation = ParseVec3(value["translation"]);
            auto rotation = ParseQuaternion(value["rotation"]);
            auto scale = ParseVec3(value["scale"]);
            if (translation.HasError() || rotation.HasError() || scale.HasError())
                return Result<Math::Transform>::Failure(PersistenceError(SceneInvalid, "Scene transform is invalid."));
            return Result<Math::Transform>::Success(
                {.translation = translation.Value(), .rotation = rotation.Value(), .scale = scale.Value()});
        }

        [[nodiscard]] Json PrimitiveParametersJson(const PrimitiveMeshDescriptor &descriptor) {
            return std::visit([]<typename Parameters>(const Parameters &parameters) {
                if constexpr (std::is_same_v<Parameters, Runtime::BoxMeshParameters>)
                    return Json{{"size", Vec3Json(parameters.size)}};
                if constexpr (std::is_same_v<Parameters, Runtime::SphereMeshParameters>)
                    return Json{{"radius", parameters.radius}, {"slices", parameters.slices}, {"stacks", parameters.stacks}};
                if constexpr (std::is_same_v<Parameters, Runtime::CapsuleMeshParameters>)
                    return Json{{"radius", parameters.radius},
                                {"totalHeight", parameters.totalHeight},
                                {"radialSegments", parameters.radialSegments},
                                {"hemisphereRings", parameters.hemisphereRings}};
                if constexpr (std::is_same_v<Parameters, Runtime::CylinderMeshParameters>)
                    return Json{{"radius", parameters.radius},
                                {"height", parameters.height},
                                {"radialSegments", parameters.radialSegments}};
                if constexpr (std::is_same_v<Parameters, Runtime::ConeMeshParameters>)
                    return Json{{"radius", parameters.radius},
                                {"height", parameters.height},
                                {"radialSegments", parameters.radialSegments}};
                if constexpr (std::is_same_v<Parameters, Runtime::PlaneMeshParameters> ||
                              std::is_same_v<Parameters, Runtime::QuadMeshParameters>)
                    return Json{{"size", Vec2Json(parameters.size)}};
                return Json::object();
            }, descriptor.parameters);
        }

        [[nodiscard]] Json PrimitiveJson(const PrimitiveMeshDescriptor &descriptor) {
            const Runtime::PrimitiveDescriptor *catalog = Runtime::PrimitiveCatalog::Find(descriptor.type);
            return Json{
                {"id", catalog == nullptr ? "" : std::string{catalog->id.value}},
                {"version", descriptor.version.value},
                {"parameters", PrimitiveParametersJson(descriptor)},
            };
        }

        [[nodiscard]] Result<PrimitiveMeshDescriptor> ParsePrimitive(const Json &value) {
            if (!value.is_object() || !value.contains("id") || !value["id"].is_string() || !value.contains("version") ||
                !value["version"].is_number_unsigned() || !value.contains("parameters") || !value["parameters"].is_object()) {
                return Result<PrimitiveMeshDescriptor>::Failure(PersistenceError(SceneInvalid, "Primitive descriptor is incomplete."));
            }
            const Runtime::PrimitiveDescriptor *catalog = Runtime::PrimitiveCatalog::Find(value["id"].get_ref<const std::string &>());
            if (catalog == nullptr || !catalog->meshType.has_value()) {
                return Result<PrimitiveMeshDescriptor>::Failure(
                    PersistenceError(SceneInvalid, "Primitive descriptor ID is not a mesh primitive."));
            }

            PrimitiveMeshDescriptor descriptor = PrimitiveMeshDescriptor::Defaults(*catalog->meshType);
            descriptor.version.value = value["version"].get<std::uint32_t>();
            if (descriptor.version.value != 1) {
                return Result<PrimitiveMeshDescriptor>::Failure(
                    PersistenceError(SceneInvalid, "Primitive descriptor version is unsupported."));
            }
            const Json &parameters = value["parameters"];
            const auto number = [&parameters](const char *key) {
                return parameters.contains(key) && parameters[key].is_number();
            };
            const auto unsignedNumber = [&parameters](const char *key) {
                return parameters.contains(key) && parameters[key].is_number_unsigned();
            };

            switch (*catalog->meshType) {
                case Runtime::PrimitiveMeshType::Box: {
                    if (!parameters.contains("size"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Box primitive parameters are incomplete."));
                    auto size = ParseVec3(parameters["size"]);
                    if (size.HasError())
                        return Result<PrimitiveMeshDescriptor>::Failure(size.ErrorValue());
                    descriptor.parameters = Runtime::BoxMeshParameters{size.Value()};
                    break;
                }
                case Runtime::PrimitiveMeshType::Sphere:
                    if (!number("radius") || !unsignedNumber("slices") || !unsignedNumber("stacks"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Sphere primitive parameters are incomplete."));
                    descriptor.parameters =
                        Runtime::SphereMeshParameters{parameters["radius"].get<float>(), parameters["slices"].get<std::uint32_t>(),
                                                      parameters["stacks"].get<std::uint32_t>()};
                    break;
                case Runtime::PrimitiveMeshType::Capsule:
                    if (!number("radius") || !number("totalHeight") || !unsignedNumber("radialSegments") ||
                        !unsignedNumber("hemisphereRings"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Capsule primitive parameters are incomplete."));
                    descriptor.parameters =
                        Runtime::CapsuleMeshParameters{parameters["radius"].get<float>(), parameters["totalHeight"].get<float>(),
                                                       parameters["radialSegments"].get<std::uint32_t>(),
                                                       parameters["hemisphereRings"].get<std::uint32_t>()};
                    break;
                case Runtime::PrimitiveMeshType::Cylinder:
                    if (!number("radius") || !number("height") || !unsignedNumber("radialSegments"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Cylinder primitive parameters are incomplete."));
                    descriptor.parameters =
                        Runtime::CylinderMeshParameters{parameters["radius"].get<float>(), parameters["height"].get<float>(),
                                                        parameters["radialSegments"].get<std::uint32_t>()};
                    break;
                case Runtime::PrimitiveMeshType::Cone:
                    if (!number("radius") || !number("height") || !unsignedNumber("radialSegments"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Cone primitive parameters are incomplete."));
                    descriptor.parameters =
                        Runtime::ConeMeshParameters{parameters["radius"].get<float>(), parameters["height"].get<float>(),
                                                    parameters["radialSegments"].get<std::uint32_t>()};
                    break;
                case Runtime::PrimitiveMeshType::Plane:
                case Runtime::PrimitiveMeshType::Quad: {
                    if (!parameters.contains("size"))
                        return Result<PrimitiveMeshDescriptor>::Failure(
                            PersistenceError(SceneInvalid, "Plane or quad primitive parameters are incomplete."));
                    auto size = ParseVec2(parameters["size"]);
                    if (size.HasError())
                        return Result<PrimitiveMeshDescriptor>::Failure(size.ErrorValue());
                    if (*catalog->meshType == Runtime::PrimitiveMeshType::Plane)
                        descriptor.parameters = Runtime::PlaneMeshParameters{size.Value()};
                    else
                        descriptor.parameters = Runtime::QuadMeshParameters{size.Value()};
                    break;
                }
            }
            return Result<PrimitiveMeshDescriptor>::Success(std::move(descriptor));
        }

        [[nodiscard]] Json ComponentsJson(const SceneObjectComponentSet &components) {
            Json value = Json::object();
            if (components.camera.has_value()) {
                const Runtime::CameraComponent &camera = *components.camera;
                value["camera"] = {
                    {"projection", camera.projection == Runtime::CameraProjection::Perspective ? "perspective" : "orthographic"},
                    {"verticalFieldOfViewRadians", camera.verticalFieldOfViewRadians},
                    {"orthographicHeight", camera.orthographicHeight},
                    {"nearPlane", camera.nearPlane},
                    {"farPlane", camera.farPlane},
                };
            }
            if (components.light.has_value()) {
                const Runtime::LightComponent &light = *components.light;
                using enum Runtime::LightKind;
                const char *kind = "spot";
                if (light.kind == Directional)
                    kind = "directional";
                else if (light.kind == Point)
                    kind = "point";
                value["light"] = {
                    {"kind", kind},
                    {"color", Vec3Json(light.color)},
                    {"intensity", light.intensity},
                    {"range", light.range},
                    {"innerConeRadians", light.innerConeRadians},
                    {"outerConeRadians", light.outerConeRadians},
                };
            }
            if (components.triggerVolume.has_value())
                value["triggerVolume"] = {{"shape", static_cast<std::uint8_t>(components.triggerVolume->shape)}};
            if (components.audioSource.has_value()) {
                const Runtime::AudioSourceComponent &audio = *components.audioSource;
                value["audioSource"] = {
                    {"kind", audio.kind == Runtime::AudioSourceKind::NativeClip ? "native_clip" : "middleware_event"},
                    {"gain", audio.gain},
                    {"spatial", audio.spatial},
                };
            }
            if (!components.behaviors.empty()) {
                Json behaviors = Json::array();
                for (const Gameplay::BehaviorComponent &behavior : components.behaviors) {
                    Json fields = Json::array();
                    for (const Gameplay::BehaviorField &field : behavior.fields)
                        fields.push_back({{"name", field.name}, {"value", BehaviorFieldValueJson(field.value)}});
                    behaviors.push_back({
                        {"instanceId", behavior.instanceId.value},
                        {"typeId", behavior.typeId.Value()},
                        {"schemaVersion", behavior.schemaVersion},
                        {"enabled", behavior.enabled},
                        {"fields", std::move(fields)},
                    });
                }
                value["behaviors"] = std::move(behaviors);
            }
            return value;
        }

        [[nodiscard]] Result<SceneObjectComponentSet> ParseComponents(const Json &value) {
            if (!value.is_object())
                return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Components must be an object."));
            SceneObjectComponentSet components;
            if (value.contains("camera")) {
                const Json &camera = value["camera"];
                if (!camera.is_object() || !camera.contains("projection") || !camera["projection"].is_string())
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Camera is invalid."));
                const std::string projection = camera["projection"].get<std::string>();
                if (projection != "perspective" && projection != "orthographic")
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Camera projection is invalid."));
                components.camera = Runtime::CameraComponent{
                    .projection =
                        projection == "perspective" ? Runtime::CameraProjection::Perspective : Runtime::CameraProjection::Orthographic,
                    .verticalFieldOfViewRadians = camera.at("verticalFieldOfViewRadians").get<float>(),
                    .orthographicHeight = camera.at("orthographicHeight").get<float>(),
                    .nearPlane = camera.at("nearPlane").get<float>(),
                    .farPlane = camera.at("farPlane").get<float>(),
                };
            }
            if (value.contains("light")) {
                const Json &light = value["light"];
                const std::string kind = light.at("kind").get<std::string>();
                auto color = ParseVec3(light.at("color"));
                if (color.HasError() || (kind != "directional" && kind != "point" && kind != "spot"))
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Light is invalid."));
                Runtime::LightKind lightKind = Runtime::LightKind::Spot;
                if (kind == "directional")
                    lightKind = Runtime::LightKind::Directional;
                else if (kind == "point")
                    lightKind = Runtime::LightKind::Point;
                components.light = Runtime::LightComponent{
                    .kind = lightKind,
                    .color = color.Value(),
                    .intensity = light.at("intensity").get<float>(),
                    .range = light.at("range").get<float>(),
                    .innerConeRadians = light.at("innerConeRadians").get<float>(),
                    .outerConeRadians = light.at("outerConeRadians").get<float>(),
                };
            }
            if (value.contains("triggerVolume")) {
                const std::uint8_t shape = value["triggerVolume"].at("shape").get<std::uint8_t>();
                if (shape > static_cast<std::uint8_t>(Runtime::ColliderShapeType::StaticPlane))
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Trigger shape is invalid."));
                components.triggerVolume = Runtime::TriggerVolumeComponent{static_cast<Runtime::ColliderShapeType>(shape)};
            }
            if (value.contains("audioSource")) {
                const Json &audio = value["audioSource"];
                const std::string kind = audio.at("kind").get<std::string>();
                if (kind != "native_clip" && kind != "middleware_event")
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Audio source is invalid."));
                components.audioSource = Runtime::AudioSourceComponent{
                    .kind = kind == "native_clip" ? Runtime::AudioSourceKind::NativeClip : Runtime::AudioSourceKind::MiddlewareEvent,
                    .gain = audio.at("gain").get<float>(),
                    .spatial = audio.at("spatial").get<bool>(),
                };
            }
            if (value.contains("behaviors")) {
                const Json &behaviors = value["behaviors"];
                if (!behaviors.is_array() || behaviors.size() > 128)
                    return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Behavior list is invalid."));
                components.behaviors.reserve(behaviors.size());
                for (const Json &behavior : behaviors) {
                    if (!behavior.is_object() || !behavior.contains("instanceId") || !behavior["instanceId"].is_number_unsigned() ||
                        !behavior.contains("typeId") || !behavior["typeId"].is_string() || !behavior.contains("schemaVersion") ||
                        !behavior["schemaVersion"].is_number_unsigned() || !behavior.contains("enabled") ||
                        !behavior["enabled"].is_boolean() || !behavior.contains("fields") || !behavior["fields"].is_array())
                        return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Behavior entry is invalid."));
                    auto typeId = Gameplay::BehaviorTypeId::Parse(behavior["typeId"].get<std::string>());
                    if (typeId.HasError())
                        return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Behavior type ID is invalid."));
                    Gameplay::BehaviorComponent parsed{
                        .instanceId = Gameplay::BehaviorInstanceId{behavior["instanceId"].get<std::uint64_t>()},
                        .typeId = std::move(typeId).Value(),
                        .schemaVersion = behavior["schemaVersion"].get<std::uint32_t>(),
                        .enabled = behavior["enabled"].get<bool>(),
                    };
                    parsed.fields.reserve(behavior["fields"].size());
                    for (const Json &fieldEntry : behavior["fields"]) {
                        if (!fieldEntry.is_object() || !fieldEntry.contains("name") || !fieldEntry["name"].is_string() ||
                            !fieldEntry.contains("value"))
                            return Result<SceneObjectComponentSet>::Failure(
                                PersistenceError(SceneInvalid, "Behavior field entry is invalid."));
                        auto field = ParseBehaviorFieldValue(fieldEntry["value"]);
                        if (field.HasError())
                            return Result<SceneObjectComponentSet>::Failure(field.ErrorValue());
                        parsed.fields.emplace_back(fieldEntry["name"].get<std::string>(), std::move(field).Value());
                    }
                    if (Gameplay::ValidateBehaviorComponent(parsed).HasError())
                        return Result<SceneObjectComponentSet>::Failure(PersistenceError(SceneInvalid, "Behavior payload is invalid."));
                    components.behaviors.push_back(std::move(parsed));
                }
            }
            return Result<SceneObjectComponentSet>::Success(std::move(components));
        }

        [[nodiscard]] Json SceneJson(const SceneDocumentSnapshot &snapshot) {
            Json objects = Json::array();
            for (const SceneObjectSnapshot &object : snapshot.objects) {
                Json value{
                    {"id", object.id.value},
                    {"parent", object.parent.has_value() ? Json(object.parent->value) : Json(nullptr)},
                    {"name", object.name},
                    {"transform", TransformJson(object.localTransform)},
                    {"components", ComponentsJson(object.components)},
                    {"editor", {{"visible", object.editorState.visible}, {"locked", object.editorState.locked}}},
                };
                value["primitiveMesh"] = object.primitiveMesh.has_value() ? PrimitiveJson(*object.primitiveMesh) : Json(nullptr);
                value["meshAsset"] = object.meshAsset.has_value() ? Json(object.meshAsset->ToString()) : Json(nullptr);
                objects.push_back(std::move(value));
            }
            return Json{{"schemaVersion", kSceneSchemaVersion}, {"objects", std::move(objects)}};
        }

        [[nodiscard]] Result<std::vector<SceneObjectSnapshot>> ParseScene(const std::string &contents) {
            try {
                const Json document = Json::parse(contents);
                if (!document.is_object() || !document.contains("schemaVersion") || document["schemaVersion"] != kSceneSchemaVersion ||
                    !document.contains("objects") || !document["objects"].is_array() || document["objects"].size() > kMaximumSceneObjects) {
                    return Result<std::vector<SceneObjectSnapshot>>::Failure(
                        PersistenceError(SceneInvalid, "Scene schema is unsupported or incomplete."));
                }

                std::vector<SceneObjectSnapshot> objects;
                objects.reserve(document["objects"].size());
                for (const Json &value : document["objects"]) {
                    if (!value.is_object() || !value.contains("id") || !value["id"].is_number_unsigned() || !value.contains("name") ||
                        !value["name"].is_string() || !value.contains("parent") || !value.contains("transform") ||
                        !value.contains("primitiveMesh") || !value.contains("components")) {
                        return Result<std::vector<SceneObjectSnapshot>>::Failure(
                            PersistenceError(SceneInvalid, "Scene object schema is incomplete."));
                    }
                    auto transform = ParseTransform(value["transform"]);
                    auto components = ParseComponents(value["components"]);
                    if (transform.HasError() || components.HasError())
                        return Result<std::vector<SceneObjectSnapshot>>::Failure(
                            PersistenceError(SceneInvalid, "Scene object values are invalid."));

                    std::optional<SceneObjectId> parent;
                    if (!value["parent"].is_null()) {
                        if (!value["parent"].is_number_unsigned())
                            return Result<std::vector<SceneObjectSnapshot>>::Failure(
                                PersistenceError(SceneInvalid, "Scene object parent is invalid."));
                        parent = SceneObjectId{value["parent"].get<std::uint64_t>()};
                    }
                    std::optional<PrimitiveMeshDescriptor> primitive;
                    if (!value["primitiveMesh"].is_null()) {
                        auto parsed = ParsePrimitive(value["primitiveMesh"]);
                        if (parsed.HasError())
                            return Result<std::vector<SceneObjectSnapshot>>::Failure(parsed.ErrorValue());
                        primitive = std::move(parsed).Value();
                    }
                    std::optional<Assets::AssetId> meshAsset;
                    if (value.contains("meshAsset") && !value["meshAsset"].is_null()) {
                        if (!value["meshAsset"].is_string())
                            return Result<std::vector<SceneObjectSnapshot>>::Failure(
                                PersistenceError(SceneInvalid, "Scene object mesh asset identity is invalid."));
                        auto parsed = Assets::AssetId::Parse(value["meshAsset"].get<std::string>());
                        if (parsed.HasError())
                            return Result<std::vector<SceneObjectSnapshot>>::Failure(
                                PersistenceError(SceneInvalid, "Scene object mesh asset identity is invalid."));
                        meshAsset = std::move(parsed).Value();
                    }
                    SceneObjectEditorState editorState;
                    if (value.contains("editor")) {
                        const Json &editor = value["editor"];
                        if (!editor.is_object() || !editor.contains("visible") || !editor["visible"].is_boolean() ||
                            !editor.contains("locked") || !editor["locked"].is_boolean()) {
                            return Result<std::vector<SceneObjectSnapshot>>::Failure(
                                PersistenceError(SceneInvalid, "Scene object editor state is invalid."));
                        }
                        editorState = SceneObjectEditorState{
                            .visible = editor["visible"].get<bool>(),
                            .locked = editor["locked"].get<bool>(),
                        };
                    }
                    objects.push_back(SceneObjectSnapshot{
                        .id = SceneObjectId{value["id"].get<std::uint64_t>()},
                        .parent = parent,
                        .name = value["name"].get<std::string>(),
                        .localTransform = transform.Value(),
                        .primitiveMesh = std::move(primitive),
                        .components = components.Value(),
                        .meshAsset = std::move(meshAsset),
                        .editorState = editorState,
                    });
                }
                return Result<std::vector<SceneObjectSnapshot>>::Success(std::move(objects));
            } catch (const Json::exception &exception) {
                return Result<std::vector<SceneObjectSnapshot>>::Failure(
                    PersistenceError(SceneInvalid, "Invalid scene JSON: " + std::string{exception.what()}));
            }
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view value) {
            const auto *begin = reinterpret_cast<const std::byte *>(value.data());
            return {begin, begin + value.size()};
        }

        [[nodiscard]] std::filesystem::path RecoveryPath(const std::filesystem::path &absoluteProjectRoot) {
            return absoluteProjectRoot / ".horo/local/recovery/default-scene.hororecovery";
        }

        [[nodiscard]] std::string SceneChecksum(const Json &scene) {
            const std::string canonical = scene.dump();
            return FormatSha256(
                ComputeSha256(std::span<const std::byte>{reinterpret_cast<const std::byte *>(canonical.data()), canonical.size()}));
        }

        [[nodiscard]] SceneFileFingerprint Fingerprint(const std::string_view bytes) {
            return SceneFileFingerprint{
                .exists = true,
                .byteSize = bytes.size(),
                .checksum = FormatSha256(
                    ComputeSha256(std::span<const std::byte>{reinterpret_cast<const std::byte *>(bytes.data()), bytes.size()})),
            };
        }
    }  // namespace

    /** @copydoc LoadProjectDefaultScene */
    Result<std::optional<LoadedProjectScene>> LoadProjectDefaultScene(const std::filesystem::path &absoluteProjectRoot) {
        const std::filesystem::path metadataPath = absoluteProjectRoot / ".horo/project.json";
        if (std::error_code error; !std::filesystem::exists(metadataPath, error)) {
            if (error)
                return Result<std::optional<LoadedProjectScene>>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to inspect '" + metadataPath.string() + "'."));
            return Result<std::optional<LoadedProjectScene>>::Success(std::nullopt);
        }

        auto metadataBytes = ReadBoundedFile(metadataPath, kMaximumProjectMetadataBytes);
        if (metadataBytes.HasError())
            return Result<std::optional<LoadedProjectScene>>::Failure(metadataBytes.ErrorValue());

        try {
            const Json metadata = Json::parse(metadataBytes.Value());
            if (!metadata.is_object() || !metadata.contains("settings") || !metadata["settings"].is_object() ||
                !metadata["settings"].contains("defaultScene") || !metadata["settings"]["defaultScene"].is_string()) {
                return Result<std::optional<LoadedProjectScene>>::Failure(
                    PersistenceError(ScenePathInvalid, "Project metadata does not contain settings.defaultScene."));
            }
            const std::filesystem::path relativeScene =
                std::filesystem::path{metadata["settings"]["defaultScene"].get<std::string>()}.lexically_normal();
            if (!IsSafeProjectRelativePath(relativeScene)) {
                return Result<std::optional<LoadedProjectScene>>::Failure(
                    PersistenceError(ScenePathInvalid, "Project defaultScene must be a safe project-relative path."));
            }
            const std::filesystem::path absoluteScene = (absoluteProjectRoot / relativeScene).lexically_normal();
            if (!absoluteScene.is_absolute() || !IsResolvedContainedBy(absoluteProjectRoot, absoluteScene)) {
                return Result<std::optional<LoadedProjectScene>>::Failure(
                    PersistenceError(ScenePathInvalid, "Resolved defaultScene is outside the project root."));
            }

            auto loaded = LoadProjectScene(absoluteProjectRoot, absoluteScene);
            if (loaded.HasError())
                return Result<std::optional<LoadedProjectScene>>::Failure(loaded.ErrorValue());
            if (!loaded.Value().existed) {
                return Result<std::optional<LoadedProjectScene>>::Failure(
                    PersistenceError(SceneReadFailed, "Configured default scene does not exist at '" + absoluteScene.string() + "'."));
            }
            return Result<std::optional<LoadedProjectScene>>::Success(std::move(loaded).Value());
        } catch (const Json::exception &exception) {
            return Result<std::optional<LoadedProjectScene>>::Failure(
                PersistenceError(SceneInvalid, "Invalid project metadata JSON: " + std::string{exception.what()}));
        }
    }

    /** @copydoc LoadProjectScene */
    Result<LoadedProjectScene> LoadProjectScene(const std::filesystem::path &absoluteProjectRoot,
                                                const std::filesystem::path &absoluteScenePath) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsResolvedContainedBy(absoluteProjectRoot, absoluteScenePath) || absoluteScenePath.extension() != ".horo") {
            return Result<LoadedProjectScene>::Failure(
                PersistenceError(ScenePathInvalid, "Scene load requires an absolute project-contained .horo path."));
        }

        if (std::error_code error; !std::filesystem::exists(absoluteScenePath, error)) {
            if (error) {
                return Result<LoadedProjectScene>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to inspect '" + absoluteScenePath.string() + "'."));
            }
            return Result<LoadedProjectScene>::Success(LoadedProjectScene{absoluteScenePath, {}, false, SceneFileFingerprint{}});
        }
        auto sceneBytes = ReadBoundedFile(absoluteScenePath, kMaximumSceneBytes);
        if (sceneBytes.HasError())
            return Result<LoadedProjectScene>::Failure(sceneBytes.ErrorValue());
        auto objects = ParseScene(sceneBytes.Value());
        if (objects.HasError())
            return Result<LoadedProjectScene>::Failure(objects.ErrorValue());
        return Result<LoadedProjectScene>::Success(
            LoadedProjectScene{absoluteScenePath, std::move(objects).Value(), true, Fingerprint(sceneBytes.Value())});
    }

    /** @copydoc InspectProjectSceneFingerprint */
    Result<SceneFileFingerprint> InspectProjectSceneFingerprint(const std::filesystem::path &absoluteProjectRoot,
                                                                const std::filesystem::path &absoluteScenePath) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsResolvedContainedBy(absoluteProjectRoot, absoluteScenePath)) {
            return Result<SceneFileFingerprint>::Failure(
                PersistenceError(ScenePathInvalid, "Scene fingerprint inspection requires an absolute project-contained path."));
        }

        if (std::error_code error; !std::filesystem::exists(absoluteScenePath, error)) {
            if (error) {
                return Result<SceneFileFingerprint>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to inspect '" + absoluteScenePath.string() + "'."));
            }
            return Result<SceneFileFingerprint>::Success(SceneFileFingerprint{});
        }
        auto bytes = ReadBoundedFile(absoluteScenePath, kMaximumSceneBytes);
        if (bytes.HasError())
            return Result<SceneFileFingerprint>::Failure(bytes.ErrorValue());
        return Result<SceneFileFingerprint>::Success(Fingerprint(bytes.Value()));
    }

    /** @copydoc SaveProjectScene */
    Result<ProjectSceneSaveResult> SaveProjectScene(const std::filesystem::path &absoluteProjectRoot,
                                                    const std::filesystem::path &absoluteScenePath, const SceneDocumentSnapshot &snapshot,
                                                    const SceneFileFingerprint &expectedFingerprint, const bool overwriteConflict,
                                                    ProjectMutationCoordinator &mutations, DurableFileSystem &files) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsResolvedContainedBy(absoluteProjectRoot, absoluteScenePath)) {
            return Result<ProjectSceneSaveResult>::Failure(
                PersistenceError(ScenePathInvalid, "Scene save destination must be an absolute path inside the project."));
        }

        auto lease = mutations.TryAcquire(ProjectMutationRequest{
            .projectRoot = absoluteProjectRoot,
            .owner = ProjectMutationOwner::Save,
            .operationId = std::format("scene-save-{}", snapshot.revision.value),
        });
        if (lease.HasError())
            return Result<ProjectSceneSaveResult>::Failure(lease.ErrorValue());

        auto currentFingerprint = InspectProjectSceneFingerprint(absoluteProjectRoot, absoluteScenePath);
        if (currentFingerprint.HasError())
            return Result<ProjectSceneSaveResult>::Failure(currentFingerprint.ErrorValue());
        if (!overwriteConflict && currentFingerprint.Value() != expectedFingerprint) {
            return Result<ProjectSceneSaveResult>::Success(ProjectSceneSaveResult{
                .status = ProjectSceneSaveStatus::Conflict,
                .fingerprint = std::move(currentFingerprint).Value(),
            });
        }

        const std::string serialized = SceneJson(snapshot).dump(2) + '\n';
        const std::vector<std::byte> bytes = Bytes(serialized);
        std::filesystem::path prepared = absoluteScenePath;
        prepared += ".save.tmp";

        if (Result<void> write = files.WriteDurable(prepared, bytes); write.HasError()) {
            static_cast<void>(files.RemoveDurable(prepared));
            return Result<ProjectSceneSaveResult>::Failure(write.ErrorValue());
        }
        currentFingerprint = InspectProjectSceneFingerprint(absoluteProjectRoot, absoluteScenePath);
        if (currentFingerprint.HasError()) {
            static_cast<void>(files.RemoveDurable(prepared));
            return Result<ProjectSceneSaveResult>::Failure(currentFingerprint.ErrorValue());
        }
        if (!overwriteConflict && currentFingerprint.Value() != expectedFingerprint) {
            static_cast<void>(files.RemoveDurable(prepared));
            return Result<ProjectSceneSaveResult>::Success(ProjectSceneSaveResult{
                .status = ProjectSceneSaveStatus::Conflict,
                .fingerprint = std::move(currentFingerprint).Value(),
            });
        }
        if (Result<void> replace = files.AtomicReplace(prepared, absoluteScenePath); replace.HasError()) {
            static_cast<void>(files.RemoveDurable(prepared));
            return Result<ProjectSceneSaveResult>::Failure(replace.ErrorValue());
        }
        return Result<ProjectSceneSaveResult>::Success(ProjectSceneSaveResult{
            .status = ProjectSceneSaveStatus::Saved,
            .fingerprint = Fingerprint(serialized),
        });
    }

    /** @copydoc SaveProjectSceneToPath */
    Result<ProjectSceneDestinationSaveResult> SaveProjectSceneToPath(const std::filesystem::path &absoluteProjectRoot,
                                                                     const std::filesystem::path &absoluteScenePath,
                                                                     const SceneDocumentSnapshot &snapshot, const bool overwriteExisting,
                                                                     ProjectMutationCoordinator &mutations, DurableFileSystem &files) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsResolvedContainedBy(absoluteProjectRoot, absoluteScenePath) || absoluteScenePath.extension() != ".horo") {
            return Result<ProjectSceneDestinationSaveResult>::Failure(
                PersistenceError(ScenePathInvalid, "Scene destination must be an absolute project-contained .horo path."));
        }

        auto initial = InspectProjectSceneFingerprint(absoluteProjectRoot, absoluteScenePath);
        if (initial.HasError()) {
            return Result<ProjectSceneDestinationSaveResult>::Failure(initial.ErrorValue());
        }
        if (initial.Value().exists && !overwriteExisting) {
            return Result<ProjectSceneDestinationSaveResult>::Success({
                .status = ProjectSceneDestinationSaveStatus::DestinationExists,
                .fingerprint = initial.Value(),
            });
        }

        auto saved = SaveProjectScene(absoluteProjectRoot, absoluteScenePath, snapshot, initial.Value(), false, mutations, files);
        if (saved.HasError()) {
            return Result<ProjectSceneDestinationSaveResult>::Failure(saved.ErrorValue());
        }
        return Result<ProjectSceneDestinationSaveResult>::Success({
            .status = saved.Value().status == ProjectSceneSaveStatus::Saved ? ProjectSceneDestinationSaveStatus::Saved
                                                                            : ProjectSceneDestinationSaveStatus::Conflict,
            .fingerprint = std::move(saved).Value().fingerprint,
        });
    }

    /** @copydoc WriteProjectSceneRecovery */
    Result<void> WriteProjectSceneRecovery(const std::filesystem::path &absoluteProjectRoot, const std::filesystem::path &absoluteScenePath,
                                           const SceneDocumentSnapshot &snapshot, const DocumentRevision savedRevision,
                                           const DocumentStateId savedState, ProjectMutationCoordinator &mutations,
                                           DurableFileSystem &files) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsContainedBy(absoluteProjectRoot, absoluteScenePath) || !savedState.IsValid()) {
            return Result<void>::Failure(
                PersistenceError(ScenePathInvalid, "Recovery destination must describe an absolute project scene."));
        }

        auto lease = mutations.TryAcquire(ProjectMutationRequest{
            .projectRoot = absoluteProjectRoot,
            .owner = ProjectMutationOwner::Autosave,
            .operationId = std::format("scene-autosave-{}", snapshot.revision.value),
        });
        if (lease.HasError())
            return Result<void>::Failure(lease.ErrorValue());

        Json scene = SceneJson(snapshot);
        const Json record{
            {"recordVersion", 1},
            {"canonicalPath", absoluteScenePath.string()},
            {"savedRevision", savedRevision.value},
            {"savedState", savedState.value},
            {"recoveredRevision", snapshot.revision.value},
            {"recoveredState", snapshot.state.value},
            {"capturedAtUnixMilliseconds",
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"sceneChecksum", SceneChecksum(scene)},
            {"scene", std::move(scene)},
        };
        const std::string serialized = record.dump(2) + '\n';
        if (serialized.size() > kMaximumRecoveryBytes) {
            return Result<void>::Failure(PersistenceError(SceneInvalid, "Recovery record exceeds the supported size limit."));
        }

        const std::filesystem::path destination = RecoveryPath(absoluteProjectRoot);
        std::filesystem::path prepared = destination;
        prepared += ".tmp";
        const std::vector<std::byte> bytes = Bytes(serialized);
        if (Result<void> write = files.WriteDurable(prepared, bytes); write.HasError()) {
            static_cast<void>(files.RemoveDurable(prepared));
            return write;
        }
        if (Result<void> replace = files.AtomicReplace(prepared, destination); replace.HasError()) {
            static_cast<void>(files.RemoveDurable(prepared));
            return replace;
        }
        return Result<void>::Success();
    }

    /** @copydoc InspectProjectSceneRecovery */
    Result<std::optional<ProjectSceneRecoveryRecord>> InspectProjectSceneRecovery(const std::filesystem::path &absoluteProjectRoot,
                                                                                  const std::filesystem::path &absoluteScenePath) {
        if (!absoluteProjectRoot.is_absolute() || !absoluteScenePath.is_absolute() ||
            !IsContainedBy(absoluteProjectRoot, absoluteScenePath)) {
            return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                PersistenceError(ScenePathInvalid, "Recovery inspection requires an absolute project scene."));
        }

        const std::filesystem::path recoveryPath = RecoveryPath(absoluteProjectRoot);
        if (std::error_code error; !std::filesystem::exists(recoveryPath, error)) {
            if (error) {
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                    PersistenceError(SceneReadFailed, "Unable to inspect recovery path '" + recoveryPath.string() + "'."));
            }
            return Result<std::optional<ProjectSceneRecoveryRecord>>::Success(std::nullopt);
        }

        auto bytes = ReadBoundedFile(recoveryPath, kMaximumRecoveryBytes);
        if (bytes.HasError())
            return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(bytes.ErrorValue());
        try {
            const Json record = Json::parse(bytes.Value());
            if (!record.is_object() || record.value("recordVersion", 0) != 1 || !record.contains("canonicalPath") ||
                !record["canonicalPath"].is_string() || !record.contains("savedRevision") ||
                !record["savedRevision"].is_number_unsigned() || !record.contains("savedState") ||
                !record["savedState"].is_number_unsigned() || !record.contains("recoveredRevision") ||
                !record["recoveredRevision"].is_number_unsigned() || !record.contains("recoveredState") ||
                !record["recoveredState"].is_number_unsigned() || !record.contains("sceneChecksum") ||
                !record["sceneChecksum"].is_string() || !record.contains("scene") || !record["scene"].is_object()) {
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                    PersistenceError(SceneInvalid, "Recovery record schema is incomplete."));
            }
            if (std::filesystem::path{record["canonicalPath"].get<std::string>()}.lexically_normal() !=
                absoluteScenePath.lexically_normal()) {
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                    PersistenceError(SceneInvalid, "Recovery record belongs to a different canonical scene."));
            }
            if (record["sceneChecksum"].get<std::string>() != SceneChecksum(record["scene"])) {
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                    PersistenceError(SceneInvalid, "Recovery scene checksum does not match its payload."));
            }

            auto objects = ParseScene(record["scene"].dump());
            if (objects.HasError())
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(objects.ErrorValue());
            ProjectSceneRecoveryRecord result{
                .absoluteCanonicalPath = absoluteScenePath,
                .savedRevision = DocumentRevision{record["savedRevision"].get<std::uint64_t>()},
                .savedState = DocumentStateId{record["savedState"].get<std::uint64_t>()},
                .recoveredRevision = DocumentRevision{record["recoveredRevision"].get<std::uint64_t>()},
                .recoveredState = DocumentStateId{record["recoveredState"].get<std::uint64_t>()},
                .objects = std::move(objects).Value(),
            };
            if (!result.savedState.IsValid() || !result.recoveredState.IsValid() || result.recoveredRevision < result.savedRevision) {
                return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                    PersistenceError(SceneInvalid, "Recovery revision metadata is invalid."));
            }
            return Result<std::optional<ProjectSceneRecoveryRecord>>::Success(std::move(result));
        } catch (const Json::exception &exception) {
            return Result<std::optional<ProjectSceneRecoveryRecord>>::Failure(
                PersistenceError(SceneInvalid, "Invalid recovery JSON: " + std::string{exception.what()}));
        }
    }

    /** @copydoc DiscardProjectSceneRecovery */
    Result<void> DiscardProjectSceneRecovery(const std::filesystem::path &absoluteProjectRoot, ProjectMutationCoordinator &mutations,
                                             DurableFileSystem &files) {
        if (!absoluteProjectRoot.is_absolute()) {
            return Result<void>::Failure(PersistenceError(ScenePathInvalid, "Recovery cleanup requires an absolute project root."));
        }
        const std::filesystem::path recoveryPath = RecoveryPath(absoluteProjectRoot);
        if (std::error_code error; !std::filesystem::exists(recoveryPath, error)) {
            if (error)
                return Result<void>::Failure(PersistenceError(SceneReadFailed, "Unable to inspect recovery state."));
            return Result<void>::Success();
        }
        auto lease = mutations.TryAcquire(ProjectMutationRequest{
            .projectRoot = absoluteProjectRoot,
            .owner = ProjectMutationOwner::Autosave,
            .operationId = "scene-recovery-discard",
        });
        if (lease.HasError())
            return Result<void>::Failure(lease.ErrorValue());
        return files.RemoveDurable(recoveryPath);
    }
}  // namespace Horo::Editor
