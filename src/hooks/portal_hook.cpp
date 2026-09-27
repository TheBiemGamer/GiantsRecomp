#include "hooks/portal_hook.h"

#include <atomic>
#include <cstdint>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#include "portal/portal_device.h"
#include "portal/portal_mode.h"
#include "portal/software/software_portal.h"
#include "portal/xbox_frame.h"

REXCVAR_DEFINE_STRING(portal_mode, "software", "Portal",
                      "Portal backend: 'software' or 'none'");
REXCVAR_DEFINE_BOOL(portal_test_figure, false, "Portal",
                    "Development: put an all-zero figure on the portal (the game reports it as a "
                    "problem toy)");

namespace {

std::atomic<giantrecomp::portal::PortalDevice*> g_portal{nullptr};

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
  if (REXCVAR_GET(portal_test_figure)) {
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

  if (auto report = ReportFromFrame(base + ctx.r4.u32)) portal->Write(*report);
  ctx.r3.u64 = 1;
}
