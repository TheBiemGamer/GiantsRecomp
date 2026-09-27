// giantrecomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>

#include "hooks/portal_hook.h"
#include "localization.h"
#include "overlay/portal_overlay_dialog.h"
#include "xex_verify.h"

class GiantrecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GiantrecompApp>(new GiantrecompApp(ctx, "giantsrecomp",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // The Xenos GPU plugin renders the Xbox 360 command stream; without it nothing is drawn.
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    // Some of the game's text (e.g. the autosave warning dialog) is already resolved by the time
    // OnPostSetup runs -- apply this as early as possible, before the XEX even loads.
    giantrecomp::ApplyLanguageSetting();
  }

  void OnPostSetup() override {
    giantrecomp::InstallConfiguredPortal(user_data_root() / "figures");
    // The F3 debug overlay's FPS line only shows once something calls this; nothing did, so it
    // silently stayed blank. Timed between successive calls (the overlay only calls this while
    // visible, once per rendered frame) rather than hooking a game-specific present function.
    SetGuestFrameStats([this] { return ComputeFrameStats(); });
  }

  void OnConfigureFonts(ImFontAtlas* atlas) override {
    // The SDK's built-in font (ProggyTiny, forced OversampleH/V=1 + PixelSnapH) is a tiny debug
    // font with no antialiasing. Add a real, readable, antialiased font (ImFontConfig's default
    // oversampling applies since we don't override it) and make it the default everywhere,
    // including the SDK's own F3/console/F4/F7 overlays.
    ImFont* font = atlas->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    if (font) ImGui::GetIO().FontDefault = font;
  }

  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override {
    (void)ui_style;
    // Replace the SDK's green terminal-console theme with ImGui's normal dark theme, plus a
    // light blue accent so it still has some identity.
    ImGui::StyleColorsDark(&imgui_style);
    imgui_style.WindowRounding = 4.0f;
    imgui_style.FrameRounding = 3.0f;
    imgui_style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.45f, 0.80f, 0.75f);
    imgui_style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.55f, 0.90f, 0.90f);
    imgui_style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.38f, 0.70f, 1.00f);
    imgui_style.Colors[ImGuiCol_Header] = ImVec4(0.20f, 0.45f, 0.80f, 0.55f);
    imgui_style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.55f, 0.90f, 0.70f);
    imgui_style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.38f, 0.70f, 0.85f);
    imgui_style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.38f, 0.70f, 1.00f);
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    rex::ui::RegisterBind("bind_portal_overlay", "F6", "Toggle the portal figure picker",
                          [this, drawer] {
                            if (portal_overlay_) {
                              portal_overlay_.reset();
                            } else {
                              portal_overlay_ = std::make_unique<giantrecomp::PortalOverlayDialog>(drawer);
                            }
                          });
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    // game_data_root is read from the cvar registry before the config file loads (see
    // ReXApp::SetupEnvironment), so --game_data_root can't be set via giantsrecomp.toml the way
    // portal_mode etc. can. Defaulting to "rom" (relative to the working directory, matching every
    // documented way of running this game) means most users need neither the flag nor a config
    // entry for it.
    if (paths.game_data_root.empty()) paths.game_data_root = "rom";
    game_data_root_ = paths.game_data_root;
  }

  void OnPostInitLogging() override {
    // ReXApp reports a missing --game_data_root itself when it builds the runtime.
    if (game_data_root_.empty()) return;
    const auto xex = game_data_root_ / "default.xex";
    // Utf8() and wide messages, not path::string(): that throws for characters outside the ANSI
    // code page, and this runs on every startup.
    REXLOG_INFO("Checking {} against the pinned SHA-256", giantrecomp::Utf8(xex));
    const auto result = giantrecomp::VerifyXex(xex, GIANTRECOMP_XEX_SHA256);
    const auto problem = giantrecomp::DescribeXexProblem(xex, result, GIANTRECOMP_XEX_SHA256);
    if (problem.empty()) {
      REXLOG_INFO("default.xex verified");
      return;
    }
    Fatal(problem);
  }

 private:
  [[noreturn]] static void Fatal(const std::wstring& message) {
    REXLOG_ERROR("{}", giantrecomp::Utf8(std::filesystem::path(message)));
    MessageBoxW(nullptr, message.c_str(), L"GiantRecomp", MB_OK | MB_ICONERROR);
    std::exit(2);
  }

  // Averaged over kFpsRefreshInterval rather than a raw frame-to-frame delta: at 60-240Hz a raw
  // instantaneous value changes every single call and is unreadable.
  static constexpr double kFpsRefreshIntervalSeconds = 0.5;

  rex::ui::FrameStats ComputeFrameStats() {
    const auto now = std::chrono::steady_clock::now();
    ++frame_stats_count_;
    if (last_frame_stats_time_) {
      fps_window_frames_++;
      fps_window_elapsed_seconds_ +=
          std::chrono::duration<double>(now - *last_frame_stats_time_).count();
      if (fps_window_elapsed_seconds_ >= kFpsRefreshIntervalSeconds) {
        cached_fps_ = fps_window_frames_ / fps_window_elapsed_seconds_;
        cached_frame_time_ms_ = (fps_window_elapsed_seconds_ / fps_window_frames_) * 1000.0;
        fps_window_frames_ = 0;
        fps_window_elapsed_seconds_ = 0.0;
      }
    }
    last_frame_stats_time_ = now;

    rex::ui::FrameStats stats;
    stats.fps = cached_fps_;
    stats.frame_time_ms = cached_frame_time_ms_;
    stats.frame_count = frame_stats_count_;
    return stats;
  }

  std::filesystem::path game_data_root_;
  std::unique_ptr<giantrecomp::PortalOverlayDialog> portal_overlay_;
  std::optional<std::chrono::steady_clock::time_point> last_frame_stats_time_;
  uint64_t frame_stats_count_ = 0;
  int fps_window_frames_ = 0;
  double fps_window_elapsed_seconds_ = 0.0;
  double cached_fps_ = 0.0;
  double cached_frame_time_ms_ = 0.0;
};
