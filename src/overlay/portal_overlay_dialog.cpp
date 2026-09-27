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
#include "portal/figure_catalog.h"
#include "portal/figure_file.h"
#include "portal/figure_stats.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/usb/usb_portal.h"
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
  // Not called automatically here: a real USB read briefly blocks the game's own polling
  // (UsbPortal::ReadAllBlocks holds a mutex shared with the game's hook thread), so it only runs
  // on an explicit user action (the Refresh button below), never just from opening the dialog.
}

void PortalOverlayDialog::Rescan() {
  figures_dir_at_last_scan_ = REXCVAR_GET(portal_figures_dir);
  entries_ = figures_dir_at_last_scan_.empty()
                 ? std::vector<portal::FigureCatalogEntry>{}
                 : portal::ScanFigureCatalog(Utf8ToPath(figures_dir_at_last_scan_));
  entry_stats_.clear();
  entry_stats_.reserve(entries_.size());
  for (const auto& entry : entries_) {
    std::optional<portal::FigureStats> stats;
    if (auto data = portal::LoadFigureFile(entry.path)) {
      stats = portal::ParseFigureStats(*data);
    }
    entry_stats_.push_back(stats);
  }
}

void PortalOverlayDialog::RefreshRealFigureStats() {
  real_figure_stats_.clear();
  portal::UsbPortal* usb = GetUsbPortal();
  if (!usb) return;
  for (int slot : usb->PresentSlots()) {
    if (auto blocks = ReadRealFigureBlocks(slot)) {
      if (auto stats = portal::ParseFigureStats(*blocks)) {
        real_figure_stats_[slot] = *stats;
      }
    }
  }
}

void PortalOverlayDialog::OnDraw(ImGuiIO& io) {
  (void)io;
  ImGui::SetNextWindowSize(ImVec2(480, 520), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Portal of Power", nullptr, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (portal::UsbPortal* usb = GetUsbPortal()) {
    ImGui::TextWrapped("Real USB portal connected.");
    if (usb->HadError()) {
      ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                         "A read or write error occurred -- see the log for details.");
    }
    ImGui::Separator();
    if (ImGui::Button("Refresh")) RefreshRealFigureStats();
    ImGui::Separator();
    const std::vector<int> present_slots = usb->PresentSlots();
    if (present_slots.empty()) {
      ImGui::TextWrapped("No figure detected on the portal.");
    } else {
      // A real portal can hold more than one figure at once (2-player co-op, items), so every
      // occupied slot is listed, not just the first.
      for (int slot : present_slots) {
        if (auto id_variant = usb->DetectedIdVariant(slot)) {
          const auto* sky = portal::FindSkylander(id_variant->first, id_variant->second);
          if (sky) {
            ImGui::Text("Slot %d: %s", slot, std::string(sky->name).c_str());
          } else {
            ImGui::Text("Slot %d: unrecognized figure (id %u, variant %u)", slot,
                        static_cast<unsigned>(id_variant->first),
                        static_cast<unsigned>(id_variant->second));
          }
        } else {
          ImGui::Text("Slot %d: figure detected, identity not read yet", slot);
        }
        if (auto it = real_figure_stats_.find(slot); it != real_figure_stats_.end()) {
          ImGui::Text("  Level %u, %u gold, \"%s\"", static_cast<unsigned>(it->second.level),
                      static_cast<unsigned>(it->second.gold), it->second.nickname.c_str());
        } else {
          ImGui::TextDisabled("  (press Refresh to read level/gold/nickname)");
        }
      }
    }
    ImGui::End();
    return;
  }
  if (portal::ParsePortalMode(REXCVAR_GET(portal_mode)) == portal::PortalMode::kUsb) {
    ImGui::TextWrapped(
        "portal_mode is 'usb' but no USB portal was found at startup. Plug it in and restart the "
        "game -- hot-plug isn't supported yet.");
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

  if (ImGui::Button(creating_ ? "Browse" : "New Figure")) creating_ = !creating_;
  ImGui::SameLine();

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

  if (creating_) {
    ImGui::InputTextWithHint("Filter", "Skylander name", filter_, sizeof(filter_));
    const std::string filter = Lower(filter_);
    ImGui::BeginChild("create_list", ImVec2(0, 0), true);
    std::string last_game;
    bool section_open = false;
    for (const auto& sky : portal::AllSkylanders()) {
      if (!filter.empty() && Lower(std::string(sky.name)).find(filter) == std::string::npos) continue;
      if (sky.game != last_game) {
        section_open = ImGui::CollapsingHeader(sky.game.data(), ImGuiTreeNodeFlags_DefaultOpen);
        last_game = std::string(sky.game);
      }
      if (!section_open) continue;
      ImGui::PushID(static_cast<int>(sky.id) * 100000 + sky.variant);
      ImGui::TextUnformatted(std::string(sky.name).c_str());
      ImGui::SameLine(ImGui::GetWindowWidth() - 80);
      if (ImGui::Button("Create")) {
        if (!CreateAndPlaceFigure(selected_slot_, sky)) {
          REXLOG_WARN("Portal overlay: could not create '{}'", sky.name);
        } else {
          Rescan();
          creating_ = false;
        }
      }
      ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
    return;
  }

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
  bool section_open = false;
  for (size_t i = 0; i < entries_.size(); ++i) {
    const auto& entry = entries_[i];
    if (!filter.empty() && Lower(entry.name).find(filter) == std::string::npos) continue;
    if (entry.game != last_game) {
      section_open = ImGui::CollapsingHeader(entry.game.empty() ? "(no game folder)" : entry.game.c_str(),
                                              ImGuiTreeNodeFlags_DefaultOpen);
      last_game = entry.game;
    }
    if (!section_open) continue;
    ImGui::PushID(entry.path.string().c_str());
    ImGui::TextUnformatted(entry.display_name.c_str());
    if (entry_stats_[i]) {
      ImGui::SameLine();
      ImGui::TextDisabled("(Lv %u, %u gold)", static_cast<unsigned>(entry_stats_[i]->level),
                          static_cast<unsigned>(entry_stats_[i]->gold));
    }
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
