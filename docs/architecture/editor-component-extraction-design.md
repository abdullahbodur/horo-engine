# EditorLayer Component Extraction Design

**Status:** Design Phase  
**Date:** 2025-01-08  
**Component:** ui/editor/EditorLayer.cpp (5754 lines, 49 Draw* functions)  
**Analysis:** Zero circular dependencies identified  

## Executive Summary

This document defines a phased extraction strategy to decompose EditorLayer.cpp into focused, testable components. Based on dependency analysis:
- **28 zero-dependency functions** (~1,000 lines) can be extracted with zero risk
- **4 module-level groupings** (Bottom Dock, Toolbar, Assets, Hierarchy) cover ~1,840 lines
- **5 phases** planned from safest (popups) to most complex (hierarchy)
- **Minimal EditorLayer changes** (<100 lines): remove Draw* declarations, add includes, delegate calls

---

## 1. File Structure & Components

### Proposed Directory Layout

```
ui/editor/
├── EditorLayer.h/cpp (coordinator, ~4,000 lines remaining)
├── EditorLayerInternal.h (existing - shared helpers)
├── components/
│   ├── EditorComponentContext.h      (NEW - shared context struct)
│   ├── EditorHelpPopup.h/cpp         (NEW - 64 lines, zero deps)
│   ├── EditorSettingsModal.h/cpp     (NEW - 60 lines, zero deps)
│   ├── EditorUIWidgets.h/cpp         (NEW - ~350 lines, 10 functions)
│   ├── EditorPopups.h/cpp            (NEW - ~420 lines, 8 popup functions)
│   ├── EditorBottomDock.h/cpp        (NEW - 574 lines, 10 functions)
│   ├── EditorToolbar.h/cpp           (NEW - 362 lines, 7 functions)
│   ├── EditorAssetsPanel.h/cpp       (NEW - 395 lines, 6 functions)
│   └── EditorHierarchy.h/cpp         (FUTURE - 656 lines, 10 functions)
```

### Component Breakdown

| Component | Functions | Est. Lines | Dependencies | Risk |
|-----------|-----------|-----------|--------------|------|
| EditorHelpPopup | DrawHelpPopup | 64 | None | Zero |
| EditorSettingsModal | DrawSettingsModal | 60 | m_mcpController, m_mcpSettingsDraft | Zero |
| EditorUIWidgets | 10× Draw* | ~350 | ImGui only | Zero |
| EditorPopups | 8× modal/popup | ~420 | Document, selection | Low |
| EditorBottomDock | DrawBottomDock + 3 tabs | 574 | Project browser, console, MCP | Low |
| EditorToolbar | DrawToolbar + 6 menus | 362 | Document, selection, gizmo | Medium |
| EditorAssetsPanel | DrawAssetsPanel + grid/tiles | 395 | Asset system, document | Medium |
| EditorHierarchy | DrawObjectsTree + nodes | 656 | Document, selection, gizmo | High |

---

## 2. Header & Include Strategy

### 2.1 EditorComponentContext Pattern

**Problem:** Components need access to EditorLayer state (m_document, m_selectedIndices, m_gizmo, etc.)

**Solution:** Single context struct passed to each component's render method.

```cpp
// ui/editor/components/EditorComponentContext.h
#pragma once
#include <vector>
#include <string>
#include <functional>

namespace Horo {
    class Registry;
    namespace Editor {
        class SceneDocument;
        class TransformGizmo;
        class EditorSchema;
        class AssetImportService;
        struct EditorWorkspaceDocument;

        // Shared context for all editor components
        struct EditorComponentContext {
            // Core document
            SceneDocument* document = nullptr;
            const SceneDocument* lastSavedDocument = nullptr;
            const EditorSchema* schema = nullptr;
            
            // Selection state
            std::vector<int>* selectedIndices = nullptr;
            std::string* selectedAssetId = nullptr;
            
            // Gizmo & transform
            TransformGizmo* gizmo = nullptr;
            GizmoMode* currentGizmoMode = nullptr;
            
            // Asset system
            AssetImportService* assetImportService = nullptr;
            
            // Workspace
            EditorWorkspaceDocument* workspaceDocument = nullptr;
            
            // Live scene
            Registry* liveRegistry = nullptr;
            
            // Callbacks
            std::function<void(const SceneObject&)>* transformCallback = nullptr;
            std::function<std::vector<std::string>()>* scriptBehaviorOptionsCallback = nullptr;
            
            // Flags
            bool* active = nullptr;
            bool* playMode = nullptr;
            bool* flyMode = nullptr;
        };
    }
}
```

### 2.2 Component Header Template

Each component follows this pattern:

```cpp
// ui/editor/components/EditorHelpPopup.h
#pragma once
#include <string>

namespace Horo::Editor {
    class EditorHelpPopup {
    public:
        void Draw();
        
        bool IsOpen() const { return m_open; }
        void SetOpen(bool open) { m_open = open; }
        void SetSearchQuery(const std::string& query) { m_searchQuery = query; }
        
    private:
        bool m_open = false;
        std::string m_searchQuery;
    };
}
```

### 2.3 Forward Declarations Strategy

- **EditorLayer.h**: Forward declare component classes, include only EditorComponentContext.h
- **Component headers**: Include only what they directly use (ImGui, STL, forward declarations)
- **Component cpp**: Include EditorLayer.h only if absolutely necessary (prefer context pattern)

**Example EditorLayer.h changes:**
```cpp
// Before
private:
    void DrawHelpPopup();
    void DrawSettingsModal();
    bool m_helpOpen = false;
    std::string m_helpSearchQuery;

// After
#include "ui/editor/components/EditorHelpPopup.h"
#include "ui/editor/components/EditorSettingsModal.h"

private:
    EditorHelpPopup m_helpPopup;
    EditorSettingsModal m_settingsModal;
```

---

## 3. EditorLayer Changes (Minimal Impact)

### 3.1 Header Changes (~40 lines)

**Remove:** 49 Draw* method declarations  
**Add:** 8 component includes + 8 component member variables  
**Net:** ~15 lines removed

### 3.2 Implementation Changes (~80 lines)

**Remove:** Move 28 function implementations (~1,800 lines) to component files  
**Add:** Component delegation in Render() (~40 lines of setup + calls)  
**Modify:** Update member access (m_helpOpen → m_helpPopup.IsOpen())  

**Example Render() delegation:**
```cpp
void EditorLayer::Render(const Camera& cam, int screenW, int screenH) {
    // ... existing viewport/dockspace setup ...
    
    // Toolbar (previously DrawToolbar())
    m_toolbar.Draw(BuildComponentContext());
    
    // Panels (previously Draw*Panel())
    if (ImGui::Begin("Assets")) {
        m_assetsPanel.Draw(BuildComponentContext());
        ImGui::End();
    }
    
    // Popups (previously DrawHelpPopup(), etc.)
    m_helpPopup.Draw();
    m_settingsModal.Draw();
    
    // ... existing gizmo/highlight rendering ...
}
```

### 3.3 Total Estimated Changes

- **Lines added:** ~120 (includes, delegation, context building)
- **Lines removed:** ~1,800 (moved to components)
- **Lines modified:** ~50 (member variable access updates)
- **Net impact:** **~70 new lines in EditorLayer.h/cpp**

---

## 4. Phased Extraction Plan

### Phase 1: Zero-Dependency Popups (Week 1)
**Risk:** Zero | **Lines:** ~124 | **Effort:** 2 days

**Files to Create:**
- `EditorHelpPopup.h/cpp` (64 lines)
- `EditorSettingsModal.h/cpp` (60 lines)

**Functions to Move:**
- `DrawHelpPopup()` - standalone popup, uses only m_helpOpen/m_helpSearchQuery
- `DrawSettingsModal()` - standalone modal, uses m_mcpSettingsDraft

**Testing:**
- Open/close help popup (Ctrl+H shortcut)
- Search filtering works
- Settings modal opens/closes
- MCP settings persist

**Success Criteria:**
- All tests pass
- No behavioral changes
- Code compiles without warnings

---

### Phase 2: UI Widget Utilities (Week 1)
**Risk:** Zero | **Lines:** ~350 | **Effort:** 3 days

**Files to Create:**
- `EditorUIWidgets.h/cpp`

**Functions to Move (10 total):**
- `DrawClipboardToast()` - standalone overlay
- `DrawHotReloadOverlay()` - standalone overlay
- `DrawStatusBar()` - standalone bottom bar
- `DrawViewGimbal()` - viewport corner widget
- `DrawConfirmDeleteObjectsModal()` - modal
- `DrawConfirmDeleteAssetModal()` - modal
- `DrawExitConfirmModal()` - modal
- `DrawRenameObjectModal()` - modal
- `DrawCommandPalettePopup()` - popup
- `DrawQuickOpenPopup()` - popup

**Dependencies:** All pure ImGui with isolated state

**Testing:**
- Each widget renders correctly
- Modals block interaction properly
- Quick open/command palette keyboard shortcuts work

---

### Phase 3: Bottom Dock (Week 2)
**Risk:** Low | **Lines:** 574 | **Effort:** 4 days

**Files to Create:**
- `EditorBottomDock.h/cpp`

**Functions to Move (10 total):**
- `DrawBottomDock()` - main container
- `DrawProjectBrowserTab()` - project explorer
- `DrawProjectBrowserBreadcrumbs()` - navigation
- `DrawProjectBrowserTiles()` - file tiles
- `DrawProjectTreeRecursive()` - tree view
- `DrawConsoleTab()` - log viewer
- `DrawMcpTab()` - MCP debug panel
- `DrawMcpClientCard()` - MCP connection status
- `DrawMcpTabLiveRequests()` - active requests
- `DrawMcpTabCatalog()` - available tools

**Dependencies:**
- Project browser: m_projectBrowserRoot, m_projectBrowserCwd, file listing cache
- Console: LogBuffer, filter flags (m_consoleShow*)
- MCP: m_mcpController

**Context Needs:**
- Project state (cwd, cache)
- Console state (filters, cache)
- MCP controller reference

**Testing:**
- Project browser navigation
- Console log filtering
- MCP tab shows correct data
- Tab switching preserves state

---

### Phase 4: Toolbar (Week 2-3)
**Risk:** Medium | **Lines:** 362 | **Effort:** 5 days

**Files to Create:**
- `EditorToolbar.h/cpp`

**Functions to Move (7 total):**
- `DrawToolbar()` - main toolbar
- `DrawToolbarFileMenu()` - File menu
- `DrawToolbarAddMenu()` - Add menu
- `DrawToolbarEditMenu()` - Edit menu
- `DrawToolbarEditMenuItems()` - Edit menu items
- `DrawToolbarViewMenu()` - View menu
- `DrawIconToolbar()` - icon-based toolbar

**Dependencies:**
- Document (new/open/save state)
- Selection (edit operations)
- Gizmo (mode selection)
- Undo/redo history

**Context Needs:**
- Full EditorComponentContext
- Menu callbacks (OnMenuNewScene, etc.)

**Testing:**
- All menu items trigger correct actions
- Keyboard shortcuts work
- Icon toolbar gizmo mode switching
- Undo/redo buttons enable/disable correctly

**Risk Mitigation:**
- Keep menu callbacks in EditorLayer (OnMenu* methods)
- Toolbar just renders UI and calls callbacks via context

---

### Phase 5: Assets Panel (Week 3)
**Risk:** Medium | **Lines:** 395 | **Effort:** 5 days

**Files to Create:**
- `EditorAssetsPanel.h/cpp`

**Functions to Move (6 total):**
- `DrawAssetsPanel()` - main panel
- `DrawAssetGrid()` - asset grid
- `DrawAssetTile()` - individual asset tile
- `DrawAssetSpotlightPopup()` - asset search
- `DrawCreateAssetModal()` - new asset modal
- `DrawCreateAssetModalContent()` - modal content

**Dependencies:**
- Asset system (AssetImportService, asset definitions)
- Selection (m_selectedAssetId)
- Draft state (m_assetDraft* fields)
- Drop zones (texture drops)

**Context Needs:**
- Full EditorComponentContext
- Asset draft state reference
- Drop zone callbacks

**Testing:**
- Asset selection works
- Create new asset modal
- Asset search/spotlight
- Texture drag-drop to assets
- Asset thumbnail rendering

**Risk Mitigation:**
- Keep asset import logic in EditorLayer
- Panel just renders UI and triggers imports via callbacks

---

### Phase 6 (Future): Hierarchy Panel
**Risk:** High | **Lines:** 656 | **Effort:** 7 days

**Reason for Deferral:** Most complex component with tight coupling to:
- Scene graph structure
- Selection logic
- Drag-drop reparenting
- Search/filtering
- Multi-scene support

**Recommended Approach:**
1. Complete Phases 1-5 first
2. Refactor hierarchy logic separately
3. Extract once patterns are proven

---

## 5. Testing Strategy

### 5.1 Existing Test Coverage

**Files:**
- `tests/test_editor.cpp` - Main editor integration tests
- `tests/test_editor_unit.cpp` - Unit tests for editor helpers

**Coverage:** Most Draw* functions are NOT directly tested (they're ImGui rendering). Tests focus on:
- Document manipulation
- Selection logic
- Asset import
- Scene serialization

### 5.2 Testing Per Phase

**Phase 1-2 (Popups & Widgets):**
- Manual testing: Open/close each popup/modal
- Automated: None needed (pure UI, no logic)
- Risk: Very low (zero dependencies)

**Phase 3 (Bottom Dock):**
- Manual: Navigate project browser, check console filtering, MCP tab
- Automated: Project browser cache invalidation logic
- Risk: Low (isolated state)

**Phase 4 (Toolbar):**
- Manual: Click all menu items, verify actions trigger
- Automated: Menu state (enable/disable based on selection)
- Risk: Medium (callbacks must work)

**Phase 5 (Assets Panel):**
- Manual: Asset selection, creation, drag-drop
- Automated: Asset import service integration
- Risk: Medium (asset system coupling)

### 5.3 New Test Files

**Consider creating:**
- `tests/test_editor_components.cpp` - Component-specific tests

**Test focus:**
- Component state management (open/close, selection)
- Context passing (components receive correct data)
- Callbacks trigger correctly

### 5.4 Regression Prevention

**After each phase:**
1. Run full test suite (`make test`)
2. Manual editor smoke test:
   - Open editor (F1)
   - Create new scene
   - Add objects (Panel, Prop, Light, Camera)
   - Select, move, rotate, scale
   - Save scene
   - Close editor
3. No crashes, no UI glitches

---

## 6. Include Dependencies & Build Impact

### 6.1 Current Include Situation

**EditorLayer.cpp includes (46 total):**
- STL: 15 headers
- ImGui: 1 header
- Horo core: 5 headers
- Horo editor: 13 headers
- Horo math: 5 headers
- Horo renderer: 10 headers
- Horo scene: 8 headers
- Horo UI: 3 headers

**Problem:** Every edit to EditorLayer.cpp recompiles ~200ms of includes

### 6.2 Component Include Reduction

**Example: EditorHelpPopup.cpp**
```cpp
// Only needs:
#include "ui/editor/components/EditorHelpPopup.h"
#include <imgui.h>
#include <string>
```

**Savings:** ~43 fewer includes per component file

### 6.3 Build Time Impact

**Before:** Editing EditorLayer.cpp = recompile 5754 lines + all includes  
**After Phase 5:** Editing EditorHelpPopup.cpp = recompile 64 lines + 3 includes

**Estimated build time improvement:**
- **Per-component edit:** 80-90% faster recompilation
- **Full rebuild:** Similar (all files recompiled anyway)
- **Incremental builds:** Massive improvement

---

## 7. Risk Assessment & Mitigations

### 7.1 Risks by Phase

| Phase | Risk | Mitigation |
|-------|------|------------|
| 1 - Popups | Zero | Standalone, no state sharing |
| 2 - Widgets | Zero | Pure ImGui, isolated state |
| 3 - Bottom Dock | Low | Copy state refs into component, validate |
| 4 - Toolbar | Medium | Keep callbacks in EditorLayer, test all menu items |
| 5 - Assets | Medium | Keep import logic in EditorLayer, thorough drop testing |
| 6 - Hierarchy | High | Defer - requires separate design effort |

### 7.2 General Risks

**Risk: Breaking existing functionality**
- **Mitigation:** Extract one component at a time, test after each
- **Mitigation:** Keep all logic in EditorLayer initially, only move rendering

**Risk: Circular dependencies**
- **Mitigation:** Use forward declarations aggressively
- **Mitigation:** Context pattern avoids direct coupling
- **Mitigation:** Dependency analysis showed zero circular deps

**Risk: State synchronization bugs**
- **Mitigation:** Components hold pointers/refs to EditorLayer state, not copies
- **Mitigation:** Context struct makes dependencies explicit

**Risk: Increased cognitive load**
- **Mitigation:** Clear naming (EditorHelpPopup vs DrawHelpPopup)
- **Mitigation:** One component = one responsibility
- **Mitigation:** Documentation in each component header

### 7.3 Rollback Plan

**If a phase fails:**
1. Revert component files (delete new files)
2. Restore EditorLayer.h/cpp from git
3. Run tests to confirm working state
4. Analyze failure, adjust design
5. Retry with lessons learned

**Cost:** ~30 minutes to rollback per phase

---

## 8. Success Metrics

### 8.1 Quantitative

- ✅ EditorLayer.cpp reduced from 5754 lines to <4000 lines
- ✅ 28+ functions extracted to components
- ✅ Zero test regressions
- ✅ Build time for component edits <50ms (vs ~200ms)
- ✅ Zero circular dependencies introduced

### 8.2 Qualitative

- ✅ Each component has clear, single responsibility
- ✅ New developers can find code faster (e.g., "toolbar code? Check EditorToolbar.cpp")
- ✅ Context pattern makes dependencies explicit
- ✅ Components are independently testable

---

## 9. Timeline & Effort

### Estimated Schedule

| Phase | Duration | Effort | Blocker |
|-------|----------|--------|---------|
| Phase 1: Popups | 2 days | Low | None |
| Phase 2: Widgets | 3 days | Low | None |
| Phase 3: Bottom Dock | 4 days | Medium | Phase 1-2 complete |
| Phase 4: Toolbar | 5 days | Medium | Phase 3 complete |
| Phase 5: Assets | 5 days | High | Phase 4 complete |
| **Total** | **~3 weeks** | **Medium-High** | Serial execution |

### Parallel Opportunities

- Phases 1-2 can run in parallel (different developers)
- Testing can overlap with next phase design

---

## 10. Open Questions & Decisions Needed

### 10.1 Component Ownership

**Question:** Should components be stateful (hold m_open, m_searchQuery) or stateless (take parameters)?

**Recommendation:** Stateful - matches current EditorLayer pattern, simpler migration

**Rationale:**
- Current code: `bool m_helpOpen` in EditorLayer
- After: `EditorHelpPopup m_helpPopup` with internal `bool m_open`
- Migration: Move field from EditorLayer to component
- Alternative (stateless): `m_helpPopup.Draw(m_helpOpen, m_helpSearchQuery)` - more invasive

### 10.2 Context Pattern vs Direct Access

**Question:** Should components access EditorLayer members directly or via context?

**Recommendation:** Context pattern for all components

**Rationale:**
- Makes dependencies explicit
- Easier to test (mock context)
- Prevents accidental coupling
- Self-documenting (context struct shows what component needs)

### 10.3 Component Granularity

**Question:** EditorUIWidgets.h bundles 10 functions - too coarse?

**Recommendation:** Start coarse, refine later

**Rationale:**
- Phase 2 validates extraction pattern with minimal files
- Can split later if needed (e.g., EditorModals.h vs EditorOverlays.h)
- Premature splitting increases file count, cognitive load

### 10.4 Header-Only Components?

**Question:** Should small components (64 lines) be header-only?

**Recommendation:** No - stick with .h/.cpp pairs

**Rationale:**
- Consistent pattern across all components
- Easier to add implementation later
- Avoids header bloat in EditorLayer.h

---

## 11. Next Steps

1. ✅ **Design approved** - this document
2. 🔲 **Create EditorComponentContext.h** - foundation for all components
3. 🔲 **Phase 1: Extract popups** - lowest risk, validates pattern
4. 🔲 **Phase 2: Extract widgets** - validates multi-function components
5. 🔲 **Phase 3-5: Extract modules** - if Phases 1-2 succeed

**First PR:** Phases 1-2 together (~500 lines extracted, 2 components)

**Recommended:** Review after Phase 2 before proceeding to Phase 3

---

## Appendix A: Function Categorization

### Zero-Dependency Functions (28 total, ~1000 lines)

**Popups (2):**
- DrawHelpPopup() - 64 lines
- DrawSettingsModal() - 60 lines

**Modals (4):**
- DrawConfirmDeleteObjectsModal() - ~70 lines
- DrawConfirmDeleteAssetModal() - ~60 lines
- DrawExitConfirmModal() - ~150 lines
- DrawRenameObjectModal() - ~60 lines

**Widgets (4):**
- DrawClipboardToast() - ~20 lines
- DrawHotReloadOverlay() - ~45 lines
- DrawStatusBar() - ~75 lines
- DrawViewGimbal() - ~155 lines

**Create/Search (4):**
- DrawCreateAssetModal() - ~15 lines
- DrawCreateAssetModalContent() - ~130 lines
- DrawCommandPalettePopup() - ~50 lines
- DrawQuickOpenPopup() - ~75 lines

### Module-Level Groupings (4 modules, ~1840 lines)

**Bottom Dock (10 functions, 574 lines):**
- DrawBottomDock()
- DrawProjectBrowserTab()
- DrawProjectBrowserBreadcrumbs()
- DrawProjectBrowserTiles()
- DrawProjectTreeRecursive()
- DrawConsoleTab()
- DrawMcpTab()
- DrawMcpClientCard()
- DrawMcpTabLiveRequests()
- DrawMcpTabCatalog()

**Toolbar (7 functions, 362 lines):**
- DrawToolbar()
- DrawToolbarFileMenu()
- DrawToolbarAddMenu()
- DrawToolbarEditMenu()
- DrawToolbarEditMenuItems()
- DrawToolbarViewMenu()
- DrawIconToolbar()

**Assets Panel (6 functions, 395 lines):**
- DrawAssetsPanel()
- DrawAssetGrid()
- DrawAssetTile()
- DrawAssetSpotlightPopup()
- DrawCreateAssetModal() (duplicate from above - belongs here)
- DrawCreateAssetModalContent() (duplicate from above)

**Hierarchy (10 functions, 656 lines):**
- DrawObjectList()
- DrawObjectsTree()
- DrawTreeNode()
- DrawSceneHeader()
- DrawSceneHeaderContextMenu()
- DrawSceneHeaderDragDrop()
- DrawObjectsTreeSearchMode()
- DrawObjectsTreeRootDropZone()
- DrawObjectsTreeRuntimeEntities()
- (1 more - needs verification)

---

## Appendix B: Member Variable Access Patterns

### Variables Accessed by Components

**Help Popup:**
- `m_helpOpen` - bool flag
- `m_helpSearchQuery` - string

**Settings Modal:**
- `m_settingsOpen` - bool flag
- `m_mcpSettingsDraft` - MCP settings object
- `m_mcpSettingsError` - string
- `m_mcpController` - reference for save/load

**Bottom Dock:**
- `m_projectBrowserRoot` - filesystem::path
- `m_projectBrowserCwd` - filesystem::path
- `m_projectDirCache` - cache map
- `m_consoleShow*` - filter bools
- `m_consoleLinesCache` - vector
- `m_mcpController` - reference

**Toolbar:**
- `m_document` - scene document
- `m_selectedIndices` - selection vector
- `m_gizmo` - transform gizmo
- `m_undoHistory` - undo stack
- `m_redoHistory` - redo stack

**Assets Panel:**
- `m_selectedAssetId` - string
- `m_assetDraft*` - 6 draft state strings
- `m_assetImportService` - reference
- `m_albedoDraftDrop` - drop zone
- `m_albedoSelDrop` - drop zone

### Migration Strategy

1. **Phase 1-2:** Move member variables into component classes
2. **Phase 3+:** Use context pattern - components get pointers to EditorLayer members
3. **Benefit:** Clear ownership (component owns UI state, EditorLayer owns document state)

---

## Appendix C: Build System Changes

### CMakeLists.txt Changes (Estimated)

```cmake
# Before (implied):
target_sources(horo-engine PRIVATE
    ui/editor/EditorLayer.cpp
    ui/editor/EditorLayer.h
)

# After Phase 5:
target_sources(horo-engine PRIVATE
    ui/editor/EditorLayer.cpp
    ui/editor/EditorLayer.h
    ui/editor/components/EditorComponentContext.h
    ui/editor/components/EditorHelpPopup.cpp
    ui/editor/components/EditorHelpPopup.h
    ui/editor/components/EditorSettingsModal.cpp
    ui/editor/components/EditorSettingsModal.h
    ui/editor/components/EditorUIWidgets.cpp
    ui/editor/components/EditorUIWidgets.h
    ui/editor/components/EditorPopups.cpp
    ui/editor/components/EditorPopups.h
    ui/editor/components/EditorBottomDock.cpp
    ui/editor/components/EditorBottomDock.h
    ui/editor/components/EditorToolbar.cpp
    ui/editor/components/EditorToolbar.h
    ui/editor/components/EditorAssetsPanel.cpp
    ui/editor/components/EditorAssetsPanel.h
)
```

**Net change:** +16 new files in target_sources

**Build impact:** None (CMake reconfigures automatically)

---

*End of Design Document*
