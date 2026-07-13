#include "Horo/Editor/EditorDataBus.h"

#include <cassert>

namespace Horo::Editor
{
    void EditorDataBus::AssertOwnerThread() const noexcept
    {
        assert(m_ownerThread == std::this_thread::get_id() && "EditorDataBus is editor-thread only.");
    }
} // namespace Horo::Editor
