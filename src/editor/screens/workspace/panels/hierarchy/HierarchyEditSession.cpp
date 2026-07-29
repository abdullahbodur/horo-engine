#include "editor/screens/workspace/panels/hierarchy/HierarchyEditSession.h"

#include <algorithm>
#include <ranges>

namespace Horo::Editor {
    namespace {
        [[nodiscard]] HierarchyNodeType ToHierarchyNodeType(const SceneObjectKind kind) noexcept {
            switch (kind) {
                case SceneObjectKind::Mesh:
                    return HierarchyNodeType::Mesh;
                case SceneObjectKind::Camera:
                    return HierarchyNodeType::Camera;
                case SceneObjectKind::Light:
                    return HierarchyNodeType::Light;
                case SceneObjectKind::TriggerVolume:
                    return HierarchyNodeType::TriggerVolume;
                case SceneObjectKind::AudioSource:
                    return HierarchyNodeType::AudioSource;
                case SceneObjectKind::Empty:
                    return HierarchyNodeType::Empty;
            }
            return HierarchyNodeType::Empty;
        }

        [[nodiscard]] EditorWorkspaceViewCommandData MakeObjectCommand(const EditorWorkspaceViewCommand command, const HierarchyNodeId id) {
            EditorWorkspaceViewCommandData result;
            result.command = command;
            result.objectPayload = SceneObjectId{id};
            return result;
        }
    }  // namespace

    /** @copydoc HierarchyEditSession::Synchronize */
    void HierarchyEditSession::Synchronize(const EditorWorkspaceViewModel &viewModel) {
        if (!m_projectionInitialized || m_projectedRevision != viewModel.documentRevision) {
            m_inputs.clear();
            m_inputs.reserve(viewModel.objects.size());
            for (const SceneObject &object : viewModel.objects) {
                m_inputs.push_back({
                    .id = object.id.value,
                    .parent = object.parent.has_value() ? std::optional<HierarchyNodeId>{object.parent->value} : std::nullopt,
                    .name = object.name,
                    .type = ToHierarchyNodeType(object.kind),
                });
            }
            m_model.Replace(m_inputs);
            m_projectedRevision = viewModel.documentRevision;
            m_projectionInitialized = true;
        }

        if (viewModel.hierarchyRevealObject.has_value() && viewModel.hierarchyRevealRevision != m_handledRevealRevision) {
            std::optional<SceneObjectId> ancestor = viewModel.hierarchyRevealObject;
            while (ancestor.has_value()) {
                const auto object = std::ranges::find(viewModel.objects, *ancestor, &SceneObject::id);
                if (object == viewModel.objects.end())
                    break;
                if (object->parent.has_value()) {
                    static_cast<void>(m_model.SetExpanded(object->parent->value, true));
                }
                ancestor = object->parent;
            }
            m_handledRevealRevision = viewModel.hierarchyRevealRevision;
        }

        if (viewModel.primarySelection.has_value()) {
            static_cast<void>(m_model.Select(viewModel.primarySelection->value));
        } else {
            m_model.ClearSelection();
        }
    }

    /** @copydoc HierarchyEditSession::VisibleRows */
    const std::vector<HierarchyVisibleRow> &HierarchyEditSession::VisibleRows(const std::string_view query) {
        m_model.BuildVisibleRows(query, m_visibleRows);
        return m_visibleRows;
    }

    /** @copydoc HierarchyEditSession::Find */
    const HierarchyNode *HierarchyEditSession::Find(const HierarchyNodeId id) const noexcept {
        return m_model.Find(id);
    }

    /** @copydoc HierarchyEditSession::SelectedId */
    std::optional<HierarchyNodeId> HierarchyEditSession::SelectedId() const noexcept {
        return m_model.SelectedId();
    }

    /** @copydoc HierarchyEditSession::Select */
    void HierarchyEditSession::Select(const HierarchyNodeId id) noexcept {
        static_cast<void>(m_model.Select(id));
    }

    /** @copydoc HierarchyEditSession::ToggleExpanded */
    void HierarchyEditSession::ToggleExpanded(const HierarchyNodeId id) noexcept {
        const HierarchyNode *node = m_model.Find(id);
        if (node != nullptr)
            static_cast<void>(m_model.SetExpanded(id, !node->expanded));
    }

    /** @copydoc HierarchyEditSession::SelectCommand */
    EditorWorkspaceViewCommandData HierarchyEditSession::SelectCommand(const HierarchyNodeId id) {
        return MakeObjectCommand(EditorWorkspaceViewCommand::SelectObject, id);
    }

    /** @copydoc HierarchyEditSession::CreateCommand */
    EditorWorkspaceViewCommandData HierarchyEditSession::CreateCommand(const Runtime::PrimitiveId primitive,
                                                                       const std::optional<SceneObjectId> parent) {
        EditorWorkspaceViewCommandData result;
        result.command = EditorWorkspaceViewCommand::CreatePrimitive;
        result.primitivePayload = primitive;
        result.objectPayload = parent;
        return result;
    }

    /** @copydoc HierarchyEditSession::DuplicateCommand */
    EditorWorkspaceViewCommandData HierarchyEditSession::DuplicateCommand(const HierarchyNodeId id) {
        return MakeObjectCommand(EditorWorkspaceViewCommand::DuplicateObject, id);
    }

    /** @copydoc HierarchyEditSession::RenameCommand */
    EditorWorkspaceViewCommandData HierarchyEditSession::RenameCommand(const HierarchyNodeId id, const std::string_view name) {
        if (!IsValidSceneObjectName(name))
            return {};

        EditorWorkspaceViewCommandData result = MakeObjectCommand(EditorWorkspaceViewCommand::UpdateObjectName, id);
        result.stringPayload = std::string{name};
        return result;
    }

    /** @copydoc HierarchyEditSession::DeleteCommand */
    EditorWorkspaceViewCommandData HierarchyEditSession::DeleteCommand(const HierarchyNodeId id) {
        return MakeObjectCommand(EditorWorkspaceViewCommand::DeleteObject, id);
    }
}  // namespace Horo::Editor
