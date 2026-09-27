#include "hooks/portal_hook.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "portal/figure_file.h"
#include "portal/portal_device.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/xbox_frame.h"

REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software' or 'none'");
REXCVAR_DEFINE_BOOL(portal_test_figure, false, "Portal",
                    "Development: put an all-zero figure on the portal (the game reports it as a "
                    "problem toy)");
REXCVAR_DEFINE_STRING(portal_figure, "", "Portal",
                      "Path to a raw 1024-byte figure dump to put on the portal (slot 0). The file "
                      "is only read; changes the game makes to the figure are not saved yet");

namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};

// portal_figure/portal_mode arrive as UTF-8; convert explicitly so non-ANSI characters survive
// (path::string() would throw for characters outside the ANSI code page).
std::filesystem::path Utf8ToPath(const std::string& utf8) {
  const std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
  return std::filesystem::path(u8);
}

}  // namespace

namespace giantrecomp {

void InstallConfiguredPortal() {
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
  auto* software = new portal::SoftwarePortal();  // intentionally never freed, see the header
  const std::string figure_path = REXCVAR_GET(portal_figure);
  if (!figure_path.empty()) {
    const std::filesystem::path path = Utf8ToPath(figure_path);
    if (auto figure = portal::LoadFigureFile(path)) {
      software->PlaceFigure(0, *figure);
      // Save the game's changes back to the same file it was loaded from. Writes are infrequent
      // (once per figure-affecting event, not per frame), so an atomic save on the calling thread
      // is cheap enough; there is no periodic or on-exit save to lose if the process is killed.
      software->SetWriteCallback([path](int slot, const portal::FigureData& data) {
        if (slot != 0) return;
        if (portal::SaveFigureFileAtomic(path, data)) {
          REXLOG_INFO("Portal: saved changes back to the figure file");
        } else {
          REXLOG_WARN("Portal: could not save changes back to the figure file");
        }
      });
      REXLOG_INFO("Portal: placed the figure from '{}' in slot 0", figure_path);
    } else {
      REXLOG_WARN("Portal: cannot load '{}' (it must be a regular file of exactly {} bytes); "
                  "running with an empty portal",
                  figure_path, portal::kFigureSize);
    }
  } else if (REXCVAR_GET(portal_test_figure)) {
    software->PlaceFigure(0, portal::FigureData{});
    REXLOG_WARN("Portal: placed an all-zero test figure in slot 0");
  }
  g_portal.store(software);
  REXLOG_INFO("Portal: software");
}

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
