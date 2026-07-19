#pragma once

/**
 * @file SceneDocument.h
 * @brief Editor-private authoritative scene state and narrow command boundary.
 */

#include "Horo/Foundation/Result.h"
#include "Horo/Math/SceneMath.h"
#include "Horo/Runtime/Scene/PrimitiveMeshDescriptor.h"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Horo::Editor
{
    /** @brief Stable identity of an authored scene object within one document session. */
    struct SceneObjectId
    {
        std::uint64_t value{0};

        /** @brief Reports whether this ID can identify an object. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const SceneObjectId&) const noexcept = default;
    };

    /** @brief Monotonic committed revision of a scene document. */
    struct DocumentRevision
    {
        std::uint64_t value{0};

        [[nodiscard]] constexpr auto operator<=>(const DocumentRevision&) const noexcept = default;
    };

    /** @brief Immutable identity of one committed authored content state. */
    struct DocumentStateId
    {
        std::uint64_t value{0};

        /** @brief Reports whether this identity denotes a committed document state. */
        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return value != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const DocumentStateId&) const noexcept = default;
    };

    using PrimitiveMeshDescriptor = Runtime::PrimitiveMeshDescriptor;

    /** @brief Typed authored component values attached to one scene object. */
    struct SceneObjectComponentSet
    {
        std::optional<Runtime::CameraComponent> camera;
        std::optional<Runtime::LightComponent> light;
        std::optional<Runtime::TriggerVolumeComponent> triggerVolume;
        std::optional<Runtime::AudioSourceComponent> audioSource;

        [[nodiscard]] constexpr auto operator<=>(const SceneObjectComponentSet&) const noexcept = default;
    };

    /** @brief Immutable value snapshot of one authored scene object. */
    struct SceneObjectSnapshot
    {
        SceneObjectId id;
        std::optional<SceneObjectId> parent;
        std::string name;
        Math::Transform localTransform;
        std::optional<PrimitiveMeshDescriptor> primitiveMesh;
        SceneObjectComponentSet components;
    };

    /** @brief Immutable value snapshot of an entire committed scene document. */
    struct SceneDocumentSnapshot
    {
        DocumentRevision revision;
        DocumentStateId state;
        std::vector<SceneObjectSnapshot> objects;
    };

    /** @brief One transient local-transform override keyed by stable scene-object identity. */
    struct SceneObjectTransformPreview
    {
        SceneObjectId object;
        Math::Transform localTransform;

        [[nodiscard]] constexpr bool operator==(const SceneObjectTransformPreview&) const noexcept = default;
    };

    /** @brief Semantic category of one committed document transition. */
    enum class DocumentChangeKind : std::uint8_t
    {
        Created,
        Renamed,
        TransformChanged,
        Duplicated,
        Deleted,
        Undone,
        Redone,
        SaveStateChanged,
    };

    /** @brief Typed request to create one authored scene object. */
    struct CreateSceneObjectCommand
    {
        std::string name;
        std::optional<SceneObjectId> parent;
        Math::Transform localTransform;
        std::optional<PrimitiveMeshDescriptor> primitiveMesh;
        SceneObjectComponentSet components;
    };

    /** @brief Catalog-based request shared by editor creation adapters. */
    struct PrimitiveCreationRequest
    {
        Runtime::PrimitiveId primitive;
        std::optional<SceneObjectId> parent;
    };

    /** @brief Typed request to rename an authored scene object. */
    struct RenameSceneObjectCommand
    {
        SceneObjectId object;
        std::string name;
    };

    /** @brief Typed request to replace an object's local transform. */
    struct SetSceneObjectTransformCommand
    {
        SceneObjectId object;
        Math::Transform localTransform;
    };

    /** @brief Typed request to duplicate one object without duplicating its children. */
    struct DuplicateSceneObjectCommand
    {
        SceneObjectId source;
        std::string name;
    };

    /** @brief Typed request to delete one object and its complete descendant subtree. */
    struct DeleteSceneObjectCommand
    {
        SceneObjectId object;
    };

    /** @brief Result metadata returned after a committed scene command. */
    struct SceneCommandResult
    {
        SceneObjectId object;
        DocumentRevision revision;
        DocumentStateId state;
        DocumentChangeKind kind{DocumentChangeKind::Created};
        std::vector<SceneObjectId> affectedObjects;
        bool committed{false};
    };

    class SceneDocumentCommandExecutor;

    /** @brief Bounded semantic undo/redo history owned by one editor document session. */
    class EditorHistory final
    {
    public:
        /** @brief Creates an empty history with bounded item and memory budgets. */
        EditorHistory();

        ~EditorHistory();

        EditorHistory(const EditorHistory&) = delete;

        EditorHistory& operator=(const EditorHistory&) = delete;

        /** @brief Reports whether one committed transaction can be undone. */
        [[nodiscard]] bool CanUndo() const noexcept;

        /** @brief Reports whether one previously undone transaction can be redone. */
        [[nodiscard]] bool CanRedo() const noexcept;

        /** @brief Clears both history branches at an explicit document load/reload boundary. */
        void Clear() noexcept;

    private:
        friend class SceneDocumentCommandExecutor;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

    /** @brief Authoritative scene authoring storage exposed publicly through immutable queries. */
    class SceneDocument final
    {
    public:
        /** @brief Returns the current committed document revision. */
        [[nodiscard]] DocumentRevision Revision() const noexcept;

        /** @brief Returns whether committed state differs from the last marked saved revision. */
        [[nodiscard]] bool IsDirty() const noexcept;

        /** @brief Returns an immutable value snapshot of the committed document. */
        [[nodiscard]] SceneDocumentSnapshot Snapshot() const;

        /**
         * @brief Returns a borrowed immutable view of committed objects for owner-thread projection work.
         * @return View valid until the next document mutation or destruction.
         */
        [[nodiscard]] std::span<const SceneObjectSnapshot> Objects() const noexcept;

        /**
         * @brief Reports whether the committed document contains @p object.
         * @param object Stable scene object identity to query.
         * @return True when the object currently exists.
         */
        [[nodiscard]] bool Contains(SceneObjectId object) const noexcept;

        /** @brief Returns the immutable identity of the currently visible authored state. */
        [[nodiscard]] DocumentStateId State() const noexcept;

        /**
         * @brief Marks a captured revision/state pair as durably saved without changing current content.
         * @param revision Monotonic revision captured by the successful save operation.
         * @param state Immutable content state written by that operation.
         * @return Success or an error when the pair cannot belong to this document session.
         */
        [[nodiscard]] Result<void> MarkSaved(DocumentRevision revision, DocumentStateId state);

    private:
        friend class SceneDocumentCommandExecutor;

        std::vector<SceneObjectSnapshot> m_objects;
        DocumentRevision m_revision{};
        DocumentRevision m_savedRevision{};
        DocumentStateId m_state{1};
        DocumentStateId m_savedState{1};
        std::uint64_t m_nextStateId{2};
        std::uint64_t m_nextObjectId{1};
    };

    /** @brief Sole mutation boundary for the minimum typed scene command set. */
    class SceneDocumentCommandExecutor final
    {
    public:
        /** @brief Creates an executor that commits commands against @p document. */
        explicit SceneDocumentCommandExecutor(SceneDocument& document, EditorHistory& history) noexcept;

        /** @brief Validates and atomically commits a create-object command. */
        [[nodiscard]] Result<SceneCommandResult> Execute(const CreateSceneObjectCommand& command);

        /** @brief Validates and atomically commits a rename-object command. */
        [[nodiscard]] Result<SceneCommandResult> Execute(const RenameSceneObjectCommand& command);

        /** @brief Validates and atomically commits a transform command. */
        [[nodiscard]] Result<SceneCommandResult> Execute(const SetSceneObjectTransformCommand& command);

        /** @brief Validates and atomically commits a shallow duplicate-object command. */
        [[nodiscard]] Result<SceneCommandResult> Execute(const DuplicateSceneObjectCommand& command);

        /** @brief Validates and atomically deletes an object subtree. */
        [[nodiscard]] Result<SceneCommandResult> Execute(const DeleteSceneObjectCommand& command);

        /** @brief Reverts the newest committed semantic history entry. */
        [[nodiscard]] Result<SceneCommandResult> Undo();

        /** @brief Reapplies the newest previously undone semantic history entry. */
        [[nodiscard]] Result<SceneCommandResult> Redo();

    private:
        SceneDocument& m_document;
        EditorHistory& m_history;
    };

    /** @brief Notification published after document content or save-state authority commits. */
    struct SceneDocumentChangedEvent
    {
        static constexpr auto HoroEventTypeName = "SceneDocumentChangedEvent";

        DocumentRevision revision;
        DocumentStateId state;
        DocumentChangeKind kind{DocumentChangeKind::Created};
        bool dirty{false};
        std::vector<SceneObjectId> affectedObjects;
    };

    /** @brief Validates catalog creation requests and commits one typed document command. */
    class CreateSceneObjectUseCase final
    {
    public:
        /** @brief Creates a catalog primitive through the supplied document mutation boundary. */
        CreateSceneObjectUseCase(SceneDocument& document, SceneDocumentCommandExecutor& executor) noexcept;

        /**
         * @brief Validates and creates one catalog primitive.
         * @param request Stable primitive identity and optional parent.
         * @return Committed command metadata or a typed validation error.
         */
        [[nodiscard]] Result<SceneCommandResult> Execute(const PrimitiveCreationRequest& request);

    private:
        SceneDocument& m_document;
        SceneDocumentCommandExecutor& m_executor;
    };
} // namespace Horo::Editor
