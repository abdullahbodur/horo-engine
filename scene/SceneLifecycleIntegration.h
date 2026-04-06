#pragma once
// Integration hooks for SceneLifecycle into the game loop.
// This demonstrates how to extend Application/GameScene to use lifecycle gates.

#include "scene/SceneLifecycle.h"

namespace Monolith {

/*
 * INTEGRATION PATTERN: How to use SceneLifecycle in CharacterApp
 *
 * Before (Current):
 *   Application::OnInit() → GameScene::Init() + LoadLevel(untracked)
 *   Application::OnUpdate() → hot-reload (untracked)
 *   → No guarantee on state consistency during transitions
 *
 * After (Proposed):
 *   Application owns SceneLifecycle m_lifecycle
 *   GameScene::Init() calls lifecycle.BeginLoading() / FinishLoading()
 *   OnUpdate() checks lifecycle.IsActive() before game updates
 *   Hot-reload calls lifecycle.BeginReloading() / CompleteReload()
 *   → All transitions are verifiable; state is explicit
 *
 * EXAMPLE USAGE:
 *
 *   // In CharacterApp (derived from Application)
 *   class CharacterApp : public Application {
 *    private:
 *       SceneLifecycle m_lifecycle;  // Add this field
 *    protected:
 *       void OnInit() override {
 *           // ... existing init ...
 *           if (!m_lifecycle.BeginLoading()) {
 *               LOG_ERROR("Failed to begin loading: %s", m_lifecycle.GetError());
 *               return;  // fail gracefully
 *           }
 *           m_gameScene.Init(m_scene, m_camera);
 *           if (!m_lifecycle.FinishLoading()) {
 *               LOG_ERROR("Failed to finish loading: %s", m_lifecycle.GetError());
 *               return;
 *           }
 *       }
 *
 *       void OnUpdate(float dt) override {
 *           if (!m_lifecycle.IsActive()) {
 *               // Scene transitioning; skip game logic
 *               m_editor.OnUpdate(dt, m_camera, ...);
 *               return;
 *           }
 *           // Normal game updates
 *           if (ShouldHotReload()) {
 *               if (!m_lifecycle.BeginReloading()) {
 *                   LOG_ERROR("Invalid reload transition");
 *                   return;
 *               }
 *               m_scene.Clear();
 *               m_gameScene.LoadLevel(newLevelDef, m_scene);
 *               if (!m_lifecycle.CompleteReload()) {
 *                   LOG_ERROR("Failed to complete reload");
 *                   return;
 *               }
 *           }
 *           // ... rest of game updates ...
 *       }
 *
 *       void OnShutdown() override {
 *           if (m_lifecycle.IsActive()) {
 *               if (!m_lifecycle.BeginUnloading()) {
 *                   LOG_ERROR("Failed to begin unloading");
 *               } else if (!m_lifecycle.FinishUnloading()) {
 *                   LOG_ERROR("Failed to finish unloading");
 *               }
 *           }
 *           // ... existing shutdown ...
 *       }
 *   };
 *
 * BENEFITS:
 *  1. All scene state transitions are gated and validated
 *  2. Bugs in lifecycle are caught immediately with explicit error messages
 *  3. Editor can query lifecycle state to prevent edits during transitions
 *  4. Future hot-reload and undo/redo are built on solid foundation
 *  5. Test coverage of lifecycle gates is separate from game logic
 *
 * WHEN TO INTEGRATE:
 *  After Phase 3 acceptance testing (lifecycle tests pass)
 *  Before Phase 5 (test-gate infrastructure requires this integration)
 */

}  // namespace Monolith
