#pragma once

#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>

#include "portal/figure_catalog.h"

struct ImGuiIO;

namespace rex::ui {
class ImGuiDrawer;
}  // namespace rex::ui

namespace giantrecomp {

// The in-game figure picker (F6). Browses .dump files under the portal_figures_dir cvar and
// places or removes the figure in slot 0 on the active software portal. Talks only to
// PlaceFigureFromFile/RemoveFigureFromSlot/GetSoftwarePortal (hooks/portal_hook.h) and
// ScanFigureCatalog (portal/figure_catalog.h); it never touches portal protocol bytes.
class PortalOverlayDialog : public rex::ui::ImGuiDialog {
 public:
  explicit PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Rescan();

  std::vector<portal::FigureCatalogEntry> entries_;
  std::string figures_dir_at_last_scan_;
  char filter_[128] = {};
};

}  // namespace giantrecomp
