#include "Engine/Core/MenuManager.h"
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace Engine {

    MenuManager::MenuManager() = default;

    void MenuManager::OnUpdate(float32 dt) {
        (void)dt;
        // 未来：动画过渡、计时器、自动关闭等
    }

    void MenuManager::OnImGui() {
        if (!isVisible || currentPage == Page::None) return;

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        switch (currentPage) {
            case Page::MainMenu: DrawMainMenuPage(); break;
            case Page::Settings: DrawSettingsPage();  break;
            case Page::Pause:    DrawPausePage();     break;
            case Page::GameUI:   DrawHUD();           break;
            default: break;
        }

        ImGui::PopStyleVar();
    }

    void MenuManager::OnRender() {
        // 未来：运行时用 OpenGL/Vulkan 自定义渲染
    }

    // ── 导航 ──

    void MenuManager::NavigateTo(Page page) {
        if (page == currentPage) return;
        m_PreviousPage = currentPage;
        currentPage = page;
        m_SelectedIndex = 0;
        m_SelectedSetting = 0;
        m_JustNavigated = true;
    }

    void MenuManager::Confirm() {
        // 由各页面绘制函数处理
    }

    void MenuManager::Back() {
        switch (currentPage) {
            case Page::Settings:
                NavigateTo(m_PreviousPage == Page::Pause ? Page::Pause : Page::MainMenu);
                break;
            case Page::Pause:
                NavigateTo(Page::GameUI);
                break;
            case Page::MainMenu:
                // 主菜单按返回 = 退出确认
                break;
            default:
                break;
        }
    }

    const char* MenuManager::PageName(Page page) {
        switch (page) {
            case Page::None:     return "None";
            case Page::MainMenu: return "Main Menu";
            case Page::Settings: return "Settings";
            case Page::Pause:    return "Pause";
            case Page::GameUI:   return "Game UI";
            default:             return "Unknown";
        }
    }

    // ── 输入处理 ──

void MenuManager::OnKeyPressed(KeyCode key) {
    if (!isVisible) return;

    switch (currentPage) {
      case Page::MainMenu:
      case Page::Pause: {
        bool isMain = (currentPage == Page::MainMenu);
        std::vector<MenuItem> items;
        if (isMain) {
          items = {
            {"New Game",  m_StartGameCallback},
            {"Settings",  [this]{ NavigateTo(Page::Settings); }},
            {"Quit",      m_QuitCallback}
          };
        } else {
          items = {
            {"Resume",    [this]{ NavigateTo(Page::GameUI); }},
            {"Settings",  [this]{ NavigateTo(Page::Settings); }},
            {"Quit",      m_QuitCallback}
          };
        }

        if (key == KeyCode::Up || key == KeyCode::W) {
          m_SelectedIndex = (m_SelectedIndex - 1 + (int32)items.size()) % (int32)items.size();
        } else if (key == KeyCode::Down || key == KeyCode::S) {
          m_SelectedIndex = (m_SelectedIndex + 1) % (int32)items.size();
        } else if (key == KeyCode::Enter || key == KeyCode::Space) {
          if (m_SelectedIndex >= 0 && m_SelectedIndex < (int32)items.size() &&
              items[m_SelectedIndex].enabled && items[m_SelectedIndex].action) {
            items[m_SelectedIndex].action();
          }
        } else if (key == KeyCode::Escape) {
          if (!isMain) Back();
        }
        break;
      }
      case Page::Settings: {
        if (key == KeyCode::Up || key == KeyCode::W) {
          m_SelectedSetting = (m_SelectedSetting - 1 + 3) % 3;
        } else if (key == KeyCode::Down || key == KeyCode::S) {
          m_SelectedSetting = (m_SelectedSetting + 1) % 3;
        } else if (key == KeyCode::Left) {
          if (m_SelectedSetting == 0) m_MasterVolume = std::max(0.0f, m_MasterVolume - 0.05f);
          if (m_SelectedSetting == 1) m_SfxVolume    = std::max(0.0f, m_SfxVolume - 0.05f);
          if (m_SelectedSetting == 2) m_Fullscreen   = !m_Fullscreen;
        } else if (key == KeyCode::Right) {
          if (m_SelectedSetting == 0) m_MasterVolume = std::min(1.0f, m_MasterVolume + 0.05f);
          if (m_SelectedSetting == 1) m_SfxVolume    = std::min(1.0f, m_SfxVolume + 0.05f);
          if (m_SelectedSetting == 2) m_Fullscreen   = !m_Fullscreen;
        } else if (key == KeyCode::Escape) {
          Back();
        }
        break;
      }
      case Page::GameUI: {
        if (key == KeyCode::Escape) NavigateTo(Page::Pause);
                break;
            }
            default: break;
        }
    }

    // ── 各页面绘制 ──

    void MenuManager::DrawMainMenuPage() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(300, 0));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;

        ImGui::Begin("MainMenu", nullptr, flags);

        ImGui::TextColored(ImVec4(1, 1, 0.5f, 1), "  GAME TITLE");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 10));

        std::vector<MenuItem> items = {
            {"New Game",  m_StartGameCallback},
            {"Settings",  [this]{ NavigateTo(Page::Settings); }},
            {"Quit",      m_QuitCallback}
        };
        DrawMenuItemList(items, m_SelectedIndex);

        ImGui::End();
    }

    void MenuManager::DrawPausePage() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(250, 0));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;

        ImGui::Begin("Pause", nullptr, flags);

        std::vector<MenuItem> items = {
            {"Resume",    [this]{ NavigateTo(Page::GameUI); }},
            {"Settings",  [this]{ NavigateTo(Page::Settings); }},
            {"Quit",      m_QuitCallback}
        };
        DrawMenuItemList(items, m_SelectedIndex);

        ImGui::End();
    }

    void MenuManager::DrawSettingsPage() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(320, 200));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;

        ImGui::Begin("Settings", nullptr, flags);
        ImGui::TextColored(ImVec4(1, 1, 0.5f, 1), "  Settings");
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 8));

        // 音量控制
        for (int i = 0; i < 3; ++i) {
            bool selected = (i == m_SelectedSetting);
            ImGui::Text("%s", selected ? ">" : " ");
            ImGui::SameLine();

            if (i == 0) {
                ImGui::Text("Master Volume: %.0f%%", m_MasterVolume * 100.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("##vol-")) {
                    m_MasterVolume = std::max(0.0f, m_MasterVolume - 0.05f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("##vol+")) {
                    m_MasterVolume = std::min(1.0f, m_MasterVolume + 0.05f);
                }
            } else if (i == 1) {
                ImGui::Text("SFX Volume:    %.0f%%", m_SfxVolume * 100.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("##sfx-")) {
                    m_SfxVolume = std::max(0.0f, m_SfxVolume - 0.05f);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("##sfx+")) {
                    m_SfxVolume = std::min(1.0f, m_SfxVolume + 0.05f);
                }
            } else {
                ImGui::Text("Fullscreen: %s", m_Fullscreen ? "ON" : "OFF");
                ImGui::SameLine();
                if (ImGui::SmallButton("Toggle")) {
                    m_Fullscreen = !m_Fullscreen;
                }
            }
        }

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::Separator();
        if (ImGui::Button("  Back  ")) Back();

        ImGui::End();
    }

    void MenuManager::DrawHUD() {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoInputs;
        ImGui::Begin("HUD", nullptr, flags);
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "ESC: Pause");
        ImGui::End();
    }

    void MenuManager::DrawMenuItemList(const std::vector<MenuItem>& items, int32& selected) {
        for (size_t i = 0; i < items.size(); ++i) {
            ImVec4 color = items[i].enabled
                ? (i == (size_t)selected ? ImVec4(1, 1, 0, 1) : ImVec4(1, 1, 1, 1))
                : ImVec4(0.5f, 0.5f, 0.5f, 1);

            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s  %s",
                         (i == (size_t)selected ? ">" : " "), items[i].label.c_str());
            ImGui::TextColored(color, "%s", buf);

            if (ImGui::IsItemClicked() && items[i].enabled && items[i].action) {
                selected = static_cast<int32>(i);
                items[i].action();
            }
        }
    }

} // namespace Engine
