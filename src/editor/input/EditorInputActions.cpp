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
        return {
            Digital(kActionSave, true, {KeyBinding(Input::Key::S, true), KeyBinding(Input::Key::S, false, false, true)}),
            Digital(kActionUndo, true, {KeyBinding(Input::Key::Z, true), KeyBinding(Input::Key::Z, false, false, true)}),
            Digital(kActionRedo, true, {KeyBinding(Input::Key::Z, true, true), KeyBinding(Input::Key::Z, false, true, true)}),
            Digital(kActionDuplicate, false, {KeyBinding(Input::Key::D, true), KeyBinding(Input::Key::D, false, false, true)}),
            Digital(kActionDelete, false, {KeyBinding(Input::Key::Delete)}),
            Digital(kActionToolSelect, true, {KeyBinding(Input::Key::Q)}),
            Digital(kActionToolMove, true, {KeyBinding(Input::Key::W)}),
            Digital(kActionToolRotate, true, {KeyBinding(Input::Key::E)}),
            Digital(kActionToolScale, true, {KeyBinding(Input::Key::R)}),
            Digital(kActionViewportFocusSelected, false, {KeyBinding(Input::Key::F)}),
            Input::ActionDescriptor{Input::ActionId{kGameplayMoveAction},
                                    Input::ActionValueType::Axis2D,
                                    Input::InputContextId{kEditorWorkspaceInputContext},
                                    false,
                                    {AxisKey(Input::Key::Left, 0, -1.0F), AxisKey(Input::Key::Right, 0, 1.0F),
                                     AxisKey(Input::Key::Down, 1, -1.0F), AxisKey(Input::Key::Up, 1, 1.0F)}},
        };
    }
}  // namespace Horo::Editor
