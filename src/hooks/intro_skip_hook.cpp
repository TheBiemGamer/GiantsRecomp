// Skips the Activision and Toys for Bob logo movies at startup.
//
// Both logos are Bink 2 files (movies/ATVInewlogo_640.mov, movies/tfb_logo_640x456.mov).
// BinkOpen (sub_82153BE8) reads the 44-byte file header into r1+96 and then parses it; the
// midasm hook at 0x82153DB0 (config/default.toml) runs in between. When the header is one of
// the two logos, recognised by the file-size and frame-count fields that are unique to them, the
// frame count at +8 is set to 1. Bink then plays one frame and reports the movie finished, so the
// game's own intro flow moves on to the title screen as if the logos had played out.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

REXCVAR_DEFINE_BOOL(skip_intro, true, "Game",
                    "Skip the Activision and Toys for Bob logo movies at startup.");

namespace {

struct LogoMovie {
  uint32_t size_field;  // header +4: file size minus 8
  uint32_t frames;      // header +8
};

constexpr LogoMovie kLogoMovies[] = {
    {0x005A25B8, 226},  // ATVInewlogo_640.mov
    {0x0052AF84, 300},  // tfb_logo_640x456.mov
};

}  // namespace

void BinkHeaderLoaded(PPCRegister& r1) {
  if (!REXCVAR_GET(skip_intro)) {
    return;
  }
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  uint8_t* header = base + r1.u32 + 96;
  // The file stores these fields little-endian, but Bink's reader has already swapped the header
  // into native (big-endian) words by the time it reaches this point.
  const uint32_t size_field = rex::memory::load_and_swap<uint32_t>(header + 4);
  const uint32_t frames = rex::memory::load_and_swap<uint32_t>(header + 8);
  for (const LogoMovie& logo : kLogoMovies) {
    if (size_field == logo.size_field && frames == logo.frames) {
      rex::memory::store_and_swap<uint32_t>(header + 8, 1);
      REXLOG_INFO("Skipping intro logo movie ({} frames)", frames);
      return;
    }
  }
}
