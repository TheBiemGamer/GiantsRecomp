#include "overlay/portal_overlay_dialog.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/imgui_drawer.h>

#include "hooks/portal_hook.h"
#include "portal/software/software_portal.h"
#include "xex_verify.h"  // giantrecomp::Utf8: path -> UTF-8, never throws on non-ANSI characters

namespace giantrecomp {

namespace {

std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

// "Slot N: <figure name>" or "Slot N: empty".
std::string SlotLabel(portal::SoftwarePortal* software, int slot) {
  std::string label = "Slot " + std::to_string(slot) + ": ";
  if (!software->Figure(slot)) return label + "empty";
  if (auto source = software->Source(slot)) return label + Utf8(source->stem());
  return label + "(unnamed)";
}

}  // namespace

PortalOverlayDialog::PortalOverlayDialog(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {
  Rescan();
}

void PortalOverlayDialog::Rescan() {
  figures_dir_at_last_scan_ = REXCVAR_GET(portal_figures_dir);
  entries_ = figures_dir_at_last_scan_.empty()
                 ? std::vector<portal::FigureCatalogEntry>{}
                 : portal::ScanFigureCatalog(Utf8ToPath(figures_dir_at_last_scan_));
}

void PortalOverlayDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Portal of Power (F6)", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  portal::SoftwarePortal* software = GetSoftwarePortal();
  if (!software) {
    ImGui::TextWrapped(
        "No software portal is active (portal_mode is not 'software'). Start the game with "
        "--portal_mode software to use the figure picker.");
    ImGui::End();
    return;
  }

  // Slot selector: also shows every slot's current figure by name.
  if (ImGui::BeginCombo("Slot", SlotLabel(software, selected_slot_).c_str())) {
    for (int i = 0; i < portal::kMaxFigures; ++i) {
      const bool selected = (i == selected_slot_);
      if (ImGui::Selectable(SlotLabel(software, i).c_str(), selected)) selected_slot_ = i;
      if (selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  const bool has_figure = software->Figure(selected_slot_).has_value();
  ImGui::BeginDisabled(!has_figure);
  if (ImGui::Button("Remove")) RemoveFigureFromSlot(selected_slot_);
  ImGui::EndDisabled();

  ImGui::Separator();

  const std::string current_dir = REXCVAR_GET(portal_figures_dir);
  if (current_dir.empty()) {
    ImGui::TextWrapped(
        "No figures folder is set. Start the game with --portal_figures_dir \"<folder>\" to "
        "browse your .dump files here.");
    ImGui::End();
    return;
  }
  if (current_dir != figures_dir_at_last_scan_) Rescan();  // the cvar can change via the console

  ImGui::InputTextWithHint("Filter", "figure name", filter_, sizeof(filter_));
  ImGui::SameLine();
  if (ImGui::Button("Rescan")) Rescan();

  if (entries_.empty()) {
    // Distinguish a typo'd or missing folder from a folder that is genuinely just empty, rather
    // than showing the same "no .dump files" message for both.
    std::error_code ec;
    const auto dir_path = Utf8ToPath(figures_dir_at_last_scan_);
    if (!std::filesystem::exists(dir_path, ec) || ec) {
      ImGui::TextWrapped("The folder '%s' does not exist.", figures_dir_at_last_scan_.c_str());
    } else if (!std::filesystem::is_directory(dir_path, ec) || ec) {
      ImGui::TextWrapped("'%s' is not a folder.", figures_dir_at_last_scan_.c_str());
    } else {
      ImGui::TextWrapped("No .dump files found under '%s'.", figures_dir_at_last_scan_.c_str());
    }
    ImGui::End();
    return;
  }

  const std::string filter = Lower(filter_);
  ImGui::BeginChild("figure_list", ImVec2(0, 0), true);
  std::string last_game;
  for (const auto& entry : entries_) {
    if (!filter.empty() && Lower(entry.name).find(filter) == std::string::npos) continue;
    if (entry.game != last_game) {
      ImGui::SeparatorText(entry.game.empty() ? "(no game folder)" : entry.game.c_str());
      last_game = entry.game;
    }
    ImGui::PushID(entry.path.string().c_str());
    ImGui::TextUnformatted(entry.name.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 80);
    if (ImGui::Button("Place")) {
      if (!PlaceFigureFromFile(selected_slot_, entry.path)) {
        REXLOG_WARN("Portal overlay: could not place '{}'", entry.path.string());
      }
    }
    ImGui::PopID();
  }
  ImGui::EndChild();

  ImGui::End();
}

}  // namespace giantrecomp
