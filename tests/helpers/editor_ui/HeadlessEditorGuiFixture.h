#pragma once

#include "Horo/Editor/EditorDataBus.h"
#include "Horo/Editor/EditorGuiContext.h"
#include "Horo/Editor/EditorSettingsService.h"
#include "Horo/Editor/EditorTheme.h"
#include "Horo/Editor/Localization/ILocalizationService.h"
#include "Horo/Foundation/DataBus.h"

#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Horo::Editor::Tests {
    class KeyLocalization final : public ILocalizationService {
    public:
        [[nodiscard]] const std::string &Get(std::string_view, std::string_view localKey) const override {
            const auto [entry, inserted] = values_.try_emplace(std::string(localKey), localKey);
            static_cast<void>(inserted);
            return entry->second;
        }

        void Set(std::string_view key, std::string value) {
            values_.insert_or_assign(std::string(key), std::move(value));
        }

    private:
        mutable std::unordered_map<std::string, std::string> values_;
    };

    class HeadlessEditorGuiFixture {
    public:
        explicit HeadlessEditorGuiFixture(const ImVec2 displaySize = {1280.0F, 800.0F}) {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            io.DisplaySize = displaySize;
            io.DeltaTime = 1.0F / 60.0F;
            io.Fonts->AddFontDefault();
            static_cast<void>(io.Fonts->Build());
            fonts_ = {.sans = io.Fonts->Fonts.front(),
                      .sansCompact = io.Fonts->Fonts.front(),
                      .sansEmphasis = io.Fonts->Fonts.front(),
                      .icon = io.Fonts->Fonts.front()};
        }

        ~HeadlessEditorGuiFixture() {
            ImGui::DestroyContext();
        }

        HeadlessEditorGuiFixture(const HeadlessEditorGuiFixture &) = delete;
        HeadlessEditorGuiFixture &operator=(const HeadlessEditorGuiFixture &) = delete;

        [[nodiscard]] const Theme::Fonts &Fonts() const noexcept {
            return fonts_;
        }

        void BeginFrame() const {
            ImGui::NewFrame();
        }

        void EndFrame() const {
            ImGui::Render();
        }

    private:
        Theme::Fonts fonts_{};
    };
}  // namespace Horo::Editor::Tests
