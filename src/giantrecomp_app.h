// giantrecomp - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include <rex/logging.h>
#include <rex/rex_app.h>

#include "xex_verify.h"

class GiantrecompApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<GiantrecompApp>(new GiantrecompApp(ctx, "giantrecomp",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // The Xenos GPU plugin renders the Xbox 360 command stream; without it nothing is drawn.
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
  }

  void OnConfigurePaths(rex::PathConfig& paths) override { game_data_root_ = paths.game_data_root; }

  void OnPostInitLogging() override {
    // ReXApp reports a missing --game_data_root itself when it builds the runtime.
    if (game_data_root_.empty()) return;
    const auto xex = game_data_root_ / "default.xex";
    REXLOG_INFO("Checking {} against the pinned SHA-256", xex.string());
    const auto result = giantrecomp::VerifyXex(xex, GIANTRECOMP_XEX_SHA256);
    switch (result.status) {
      case giantrecomp::XexCheck::Match:
        REXLOG_INFO("default.xex verified");
        return;
      case giantrecomp::XexCheck::Unreadable:
        Fatal("Cannot read " + xex.string() +
              "\nPut your extracted Skylanders Giants disc contents in the game folder "
              "(default.xex must be at its top level).");
      case giantrecomp::XexCheck::Mismatch:
        Fatal("default.xex is not the supported build.\nExpected SHA-256: " +
              std::string(GIANTRECOMP_XEX_SHA256) + "\nFound SHA-256:    " + result.actual_sha256);
      case giantrecomp::XexCheck::BadExpected:
        Fatal("Internal error: the pinned SHA-256 is malformed.");
    }
  }

 private:
  [[noreturn]] static void Fatal(const std::string& message) {
    REXLOG_ERROR("{}", message);
    MessageBoxA(nullptr, message.c_str(), "GiantRecomp", MB_OK | MB_ICONERROR);
    std::exit(2);
  }

  std::filesystem::path game_data_root_;
};
