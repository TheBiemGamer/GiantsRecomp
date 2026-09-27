#include "hooks/portal_hook.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "portal/figure_file.h"
#include "portal/portal_device.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/usb/usb_portal.h"
#include "portal/xbox_frame.h"

REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software', 'usb' (experimental -- opens but the game does "
                      "not yet recognize the portal as present, see "
                      "docs/investigation/portal-protocol.md), or 'none'");
REXCVAR_DEFINE_BOOL(portal_test_figure, false, "Portal",
                    "Development: put an all-zero figure on the portal (the game reports it as a "
                    "problem toy)");
REXCVAR_DEFINE_STRING(portal_figure, "", "Portal",
                      "Path to a raw 1024-byte figure dump to put on the portal (slot 0) at "
                      "startup. Changes the game makes to it are saved back to this file.");
REXCVAR_DEFINE_STRING(portal_figures_dir, "", "Portal",
                      "Folder to search for .dump figure files for the in-game figure picker "
                      "(F6). Searched recursively; only used by the overlay.");

namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};
std::atomic<giantrecomp::portal::SoftwarePortal*> g_software_portal{nullptr};

// portal_figure/portal_figures_dir arrive as UTF-8; convert explicitly so non-ANSI characters
// survive (path::string() would throw for characters outside the ANSI code page).
std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

}  // namespace

namespace giantrecomp {

void InstallConfiguredPortal(const std::filesystem::path& default_figures_dir) {
  if (REXCVAR_GET(portal_figures_dir).empty()) {
    std::error_code ec;
    std::filesystem::create_directories(default_figures_dir, ec);
    const auto u8 = default_figures_dir.u8string();
    REXCVAR_SET(portal_figures_dir, std::string(reinterpret_cast<const char*>(u8.data()), u8.size()));
  }

  const std::string text = REXCVAR_GET(portal_mode);
  const auto mode = portal::ParsePortalMode(text);
  if (!mode) {
    REXLOG_WARN("Unknown portal_mode '{}'; running with no portal (use 'software' or 'none')", text);
    return;
  }
  if (*mode == portal::PortalMode::kNone) {
    REXLOG_INFO("Portal: none");
    return;
  }
  if (*mode == portal::PortalMode::kUsb) {
    auto* usb = new portal::UsbPortal();  // intentionally never freed, matching the software path
    if (!usb->IsOpen()) {
      REXLOG_WARN("Portal: no USB portal found (checked known Skylanders portal VID/PIDs); "
                  "running with no portal");
      delete usb;
      return;
    }
    // g_software_portal is intentionally left null here: it is a SoftwarePortal-only status
    // handle (used by GetSoftwarePortal() for the figure-picker overlay), and there is no
    // software portal active in this mode.
    g_portal.store(usb);
    REXLOG_WARN("Portal: usb (experimental -- opened the device, but the game is not yet known "
                "to recognize it as present; see docs/investigation/portal-protocol.md)");
    return;
  }

  auto* software = new portal::SoftwarePortal();  // intentionally never freed, see the header
  // The source path each slot's figure was loaded from (if any) is tracked by SoftwarePortal
  // itself, set atomically with the figure's data — see PlaceFigure's doc comment for why that
  // matters. This callback just saves whatever source it is handed.
  software->SetWriteCallback([](int slot, const portal::FigureData& data,
                                const std::optional<std::filesystem::path>& source) {
    if (!source) return;  // this slot's figure did not come from a file
    if (portal::SaveFigureFileAtomic(*source, data)) {
      REXLOG_INFO("Portal: saved changes back to slot {}'s figure file", slot);
    } else {
      REXLOG_WARN("Portal: could not save changes back to slot {}'s figure file", slot);
    }
  });

  // Publish the portal before placing the startup figure: InstallConfiguredPortal runs on the
  // app's setup thread, before any guest thread exists to call the hooks, so this ordering cannot
  // race with a hook call.
  g_software_portal.store(software);
  g_portal.store(software);

  const std::string figure_path = REXCVAR_GET(portal_figure);
  if (!figure_path.empty()) {
    if (!PlaceFigureFromFile(0, Utf8ToPath(figure_path))) {
      REXLOG_WARN("Portal: cannot load '{}' (it must be a regular file of exactly {} bytes); "
                  "running with an empty portal",
                  figure_path, portal::kFigureSize);
    }
  } else if (REXCVAR_GET(portal_test_figure)) {
    software->PlaceFigure(0, portal::FigureData{});
    REXLOG_WARN("Portal: placed an all-zero test figure in slot 0");
  }
  REXLOG_INFO("Portal: software");
}

bool PlaceFigureFromFile(int slot, const std::filesystem::path& path) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  auto figure = portal::LoadFigureFile(path);
  if (!figure) return false;
  return software->PlaceFigure(slot, *figure, path);
}

bool RemoveFigureFromSlot(int slot) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  return software->RemoveFigure(slot);
}

bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky) {
  portal::SoftwarePortal* software = g_software_portal.load();
  if (!software) return false;
  const std::string dir_utf8 = REXCVAR_GET(portal_figures_dir);
  if (dir_utf8.empty()) return false;

  const std::filesystem::path game_dir = Utf8ToPath(dir_utf8) / std::string(sky.game);
  std::error_code ec;
  std::filesystem::create_directories(game_dir, ec);
  if (ec) return false;

  const std::filesystem::path path = portal::UniqueFigurePath(game_dir, sky.name);
  const portal::FigureData data = portal::CreateBlankFigure(sky.id, sky.variant);
  if (!portal::SaveFigureFileAtomic(path, data)) return false;
  return PlaceFigureFromFile(slot, path);
}

portal::SoftwarePortal* GetSoftwarePortal() { return g_software_portal.load(); }

}  // namespace giantrecomp

// The game reads and writes its portal through two small recompiled wrappers (see
// docs/investigation/portal-api.md and portal-protocol.md). Replace them when a portal is installed.
REX_EXTERN(__imp__sub_82403B18);  // initializer: sets the "portal API available" flag
REX_EXTERN(__imp__sub_82403BB8);  // read:  r3 = &bytes_read, r4 = &buffer_size, r5 = buffer
REX_EXTERN(__imp__sub_82403C28);  // write: r4 = frame buffer

REX_HOOK_RAW(sub_82403BB8) {
  using namespace giantrecomp::portal;
  PortalDevice* portal = g_portal.load();
  if (!portal) {
    __imp__sub_82403BB8(ctx, base);
    return;
  }
  const uint32_t bytes_read_ptr = ctx.r3.u32;
  const uint32_t size_ptr = ctx.r4.u32;
  const uint32_t buffer_ptr = ctx.r5.u32;

  // The game passes its buffer size (0x20) through a pointer, big-endian.
  const uint8_t* size = base + size_ptr;
  const uint32_t buffer_size = (uint32_t(size[0]) << 24) | (uint32_t(size[1]) << 16) |
                               (uint32_t(size[2]) << 8) | uint32_t(size[3]);
  if (buffer_size < kFrameSize) {
    ctx.r3.u64 = 0;  // buffer too small for a frame: report failure
    return;
  }

  PPCContext init_ctx = ctx;  // the initializer clobbers registers; run it on a copy
  __imp__sub_82403B18(init_ctx, base);

  FrameFromReport(portal->Read(), base + buffer_ptr);
  uint8_t* bytes_read = base + bytes_read_ptr;  // the game expects the byte count here, big-endian
  bytes_read[0] = 0;
  bytes_read[1] = 0;
  bytes_read[2] = 0;
  bytes_read[3] = static_cast<uint8_t>(kFrameSize);
  ctx.r3.u64 = 1;
}

REX_HOOK_RAW(sub_82403C28) {
  using namespace giantrecomp::portal;
  PortalDevice* portal = g_portal.load();
  if (!portal) {
    __imp__sub_82403C28(ctx, base);
    return;
  }
  PPCContext init_ctx = ctx;
  __imp__sub_82403B18(init_ctx, base);

  if (auto report = ReportFromFrame(base + ctx.r4.u32)) {
    // LED updates ('C') arrive ~10 times a second; leave them out. Use --log_level debug to see the rest.
    if ((*report)[0] != 'C') {
      REXLOG_DEBUG("Portal write: {:02x} {:02x} {:02x}", (*report)[0], (*report)[1], (*report)[2]);
    }
    portal->Write(*report);
  }
  ctx.r3.u64 = 1;
}
