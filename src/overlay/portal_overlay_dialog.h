#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "portal/figure_catalog.h"
#include "portal/figure_stats.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace giantrecomp {

// The in-game figure picker (F6). Browses .dump files under the portal_figures_dir cvar and
// places or removes a figure in a chosen slot (0-15) on the active software portal. Talks only to
// PlaceFigureFromFile/RemoveFigureFromSlot/GetSoftwarePortal (hooks/portal_hook.h) and
// ScanFigureCatalog (portal/figure_catalog.h); it never touches portal protocol bytes.
class PortalOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Rescan();
  void RefreshRealFigureStats();

  std::vector<portal::FigureCatalogEntry> entries_;
  std::vector<std::optional<portal::FigureStats>> entry_stats_;  // parallel to entries_
  std::string figures_dir_at_last_scan_;
  char filter_[128] = {};
  int selected_slot_ = 0;  // which slot Place/Remove act on
  bool creating_ = false;  // false: Browse tab: true: New Figure tab
  std::unordered_map<int, portal::FigureStats> real_figure_stats_;  // slot -> decoded stats
};

}  // namespace giantrecomp
