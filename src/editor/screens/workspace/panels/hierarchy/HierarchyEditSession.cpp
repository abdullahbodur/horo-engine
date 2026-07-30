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
                case SceneObjectKind::GameObject:
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
        m_selectedObjects = viewModel.selectedObjects;
        if (m_selectedObjects.empty() && viewModel.primarySelection.has_value())
            m_selectedObjects.push_back(*viewModel.primarySelection);
        if (!m_selectionAnchor.has_value() && viewModel.primarySelection.has_value()) {
            m_selectionAnchor = viewModel.primarySelection->value;
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

    /** @copydoc HierarchyEditSession::IsSelected */
    bool HierarchyEditSession::IsSelected(const HierarchyNodeId id) const noexcept {
        return std::ranges::find(m_selectedObjects, SceneObjectId{id}) != m_selectedObjects.end();
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
    EditorWorkspaceViewCommandData HierarchyEditSession::SelectCommand(const HierarchyNodeId id, const HierarchySelectionGesture gesture) {
        std::vector<SceneObjectId> selected;
        const SceneObjectId clicked{id};
        if (gesture == HierarchySelectionGesture::Toggle) {
            selected = m_selectedObjects;
            const auto existing = std::ranges::find(selected, clicked);
            if (existing == selected.end()) {
                selected.push_back(clicked);
            } else {
                selected.erase(existing);
            }
            m_selectionAnchor = id;
        } else if (gesture == HierarchySelectionGesture::Range && m_selectionAnchor.has_value()) {
            const auto anchor = std::ranges::find_if(m_visibleRows, [this](const HierarchyVisibleRow &row) {
                return row.node != nullptr && row.node->id == *m_selectionAnchor;
            });
            const auto target = std::ranges::find_if(m_visibleRows, [id](const HierarchyVisibleRow &row) {
                return row.node != nullptr && row.node->id == id;
            });
            if (anchor != m_visibleRows.end() && target != m_visibleRows.end()) {
                const auto first = std::min(anchor, target);
                const auto last = std::max(anchor, target);
                selected.reserve(static_cast<std::size_t>(std::distance(first, last)) + 1);
                for (auto row = first; row != std::next(last); ++row) {
                    selected.push_back(SceneObjectId{row->node->id});
                }
            }
        }
        if (selected.empty() && gesture != HierarchySelectionGesture::Toggle) {
            selected.push_back(clicked);
            m_selectionAnchor = id;
        }

        std::optional<SceneObjectId> primary;
        if (std::ranges::find(selected, clicked) != selected.end()) {
            primary = clicked;
        } else if (!selected.empty()) {
            primary = selected.back();
        }

        EditorWorkspaceViewCommandData result;
        result.command = EditorWorkspaceViewCommand::SelectObject;
        result.objectSelection = ObjectSelectionRequest{.objects = std::move(selected), .primary = primary};
        return result;
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
