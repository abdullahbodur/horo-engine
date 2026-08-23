#include "EditorInputActions.h"

namespace Horo::Editor {
    namespace {
        Input::InputBinding KeyBinding(const Input::Key key, const bool control = false, const bool shift = false,
                                       const bool command = false) {
            return Input::InputBinding{.kind = Input::BindingControlKind::Key,
                                       .key = key,
                                       .requiredModifiers = {.control = control, .shift = shift, .command = command}};
        }

        Input::ActionDescriptor Digital(const char *id, const bool required, std::vector<Input::InputBinding> bindings) {
            return Input::ActionDescriptor{Input::ActionId{id}, Input::ActionValueType::Digital,
                                           Input::InputContextId{kEditorWorkspaceInputContext}, required, std::move(bindings)};
        }

        Input::InputBinding AxisKey(const Input::Key key, const std::uint8_t component, const float scale) {
            Input::InputBinding binding = KeyBinding(key);
            binding.component = component;
            binding.scale = scale;
            return binding;
        }
    }  // namespace

    std::vector<Input::ActionDescriptor> BuildEditorInputActions() {
        using enum Input::Key;
        return {
            Digital(kActionSave, true, {KeyBinding(S, true), KeyBinding(S, false, false, true)}),
            Digital(kActionUndo, true, {KeyBinding(Z, true), KeyBinding(Z, false, false, true)}),
            Digital(kActionRedo, true, {KeyBinding(Z, true, true), KeyBinding(Z, false, true, true)}),
            Digital(kActionDuplicate, false, {KeyBinding(D, true), KeyBinding(D, false, false, true)}),
            Digital(kActionDelete, false, {KeyBinding(Delete)}),
            Digital(kActionToolSelect, true, {KeyBinding(Q)}),
            Digital(kActionToolMove, true, {KeyBinding(W)}),
            Digital(kActionToolRotate, true, {KeyBinding(E)}),
            Digital(kActionToolScale, true, {KeyBinding(R)}),
            Digital(kActionViewportFocusSelected, false, {KeyBinding(F)}),
            Input::ActionDescriptor{Input::ActionId{kGameplayMoveAction},
                                    Input::ActionValueType::Axis2D,
                                    Input::InputContextId{kEditorWorkspaceInputContext},
                                    false,
                                    {AxisKey(Left, 0, -1.0F), AxisKey(Right, 0, 1.0F), AxisKey(Down, 1, -1.0F), AxisKey(Up, 1, 1.0F)}},
        };
    }

}  // namespace Horo::Editor
