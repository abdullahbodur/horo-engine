#include "EditorInputActions.h"

namespace Horo::Editor {
    namespace {
        Input::InputBinding KeyBinding(const Input::Key key, const bool control = false, const bool shift = false,
                                       const bool command = false) {
            return Input::InputBinding{
                .kind = Input::BindingControlKind::Key,
                .key = key,
                .requiredModifiers = {.control = control, .shift = shift, .command = command}
            };
        }

        Input::ActionDescriptor Digital(const char *id, const bool required,
                                        std::vector<Input::InputBinding> bindings) {
            return Input::ActionDescriptor{
                Input::ActionId{id}, Input::ActionValueType::Digital,
                Input::InputContextId{kEditorWorkspaceInputContext}, required, std::move(bindings)
            };
        }
    } // namespace

    std::vector<Input::ActionDescriptor> BuildEditorInputActions() {
        return {
            Digital(kActionSave, true, {
                        KeyBinding(Input::Key::S, true), KeyBinding(Input::Key::S, false, false, true)
                    }),
            Digital(kActionUndo, true, {
                        KeyBinding(Input::Key::Z, true), KeyBinding(Input::Key::Z, false, false, true)
                    }),
            Digital(kActionRedo, true,
                    {KeyBinding(Input::Key::Z, true, true), KeyBinding(Input::Key::Z, false, true, true)}),
            Digital(kActionDuplicate, false,
                    {KeyBinding(Input::Key::D, true), KeyBinding(Input::Key::D, false, false, true)}),
            Digital(kActionDelete, false, {KeyBinding(Input::Key::Delete)}),
            Digital(kActionToolSelect, true, {KeyBinding(Input::Key::Q)}),
            Digital(kActionToolMove, true, {KeyBinding(Input::Key::W)}),
            Digital(kActionToolRotate, true, {KeyBinding(Input::Key::E)}),
            Digital(kActionToolScale, true, {KeyBinding(Input::Key::R)}),
            Digital(kActionViewportFocusSelected, false, {KeyBinding(Input::Key::F)}),
        };
    }
} // namespace Horo::Editor
