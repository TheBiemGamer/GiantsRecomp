#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include <hidapi.h>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A small set of VID/PID pairs known to be a real Skylanders Portal of Power, matching Cemu's own
// nsyshid whitelist for this device family (read for this fact only, not copied code).
//
// Only the Wii U portal (1430:0150) has actually been tested (docs/architecture.md, "Portal
// layering"). The Xbox 360 portal (1430:1F17) is whitelisted on the same basis Cemu whitelists it,
// but this project has no such hardware to verify against -- it may use a different report size or
// need a different command mechanism than what was found for the Wii U portal.
inline constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal -- tested, works
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal -- untested
};

// A real, physical Portal of Power, opened over USB HID via hidapi -- no WinUSB/Zadig driver
// replacement needed; hid_send_output_report() reaches the device through the stock HID class
// driver (docs/portal-protocol.md).
//
// Commands go out via hid_send_output_report() (a HID SET_REPORT control transfer) -- not
// hid_write(), which prefers this device's interrupt OUT endpoint and is silently ignored by its
// firmware. Reads use plain hid_read_timeout(); the real device's report is exactly 32 bytes both
// ways, matching PortalDevice::Report directly.
class UsbPortal final : public PortalDevice {
 public:
  // Opens the first connected device matching kKnownPortals, in the order listed. Check IsOpen()
  // before installing this as the active portal -- construction never throws or logs; the caller
  // decides what to do if no matching device was found.
  UsbPortal();
  ~UsbPortal() override;

  UsbPortal(const UsbPortal&) = delete;
  UsbPortal& operator=(const UsbPortal&) = delete;

  bool IsOpen() const { return device_ != nullptr; }

  void Write(const Report& report) override;
  Report Read() override;

  // Best-effort status, derived by passively observing replies that already flow through Read()
  // as part of relaying the game's own polling -- no extra USB traffic, no separate poller thread
  // competing with the game's hook thread for the device. Reflects what the game itself has seen
  // so far: it can lag a few seconds behind the real device, since it only updates once the game
  // happens to poll a status frame or read block 1 (where id/variant live) on its own.
  //
  // Tracked per slot (0-15, matching kMaxFigures): a real portal can hold more than one figure at
  // once (Giants supports 2-player co-op plus items), so slot 0 alone is not the whole picture.
  bool FigurePresent() const;                // true if any slot holds a figure
  std::vector<int> PresentSlots() const;      // every slot index that currently holds a figure
  std::optional<std::pair<uint16_t, uint16_t>> DetectedIdVariant(int slot) const;
  bool HadError() const { return write_error_logged_.load() || read_error_logged_.load(); }

  // Reads all 64 blocks of the figure in `slot` directly, independent of the game's own polling.
  // Returns nullopt on any timeout, I/O error, out-of-range slot, or no device open. Acquires
  // io_mutex_ for the whole sequence, so it briefly blocks the game's own Write()/Read() calls --
  // on-demand only (e.g. an overlay Refresh button), never called from a per-frame path.
  std::optional<FigureData> ReadAllBlocks(int slot);

 private:
  void ObserveReply(const Report& report);

  void SendRaw(const Report& report);  // unlocked -- callers hold io_mutex_
  Report ReceiveRaw();                 // unlocked -- callers hold io_mutex_

  std::mutex io_mutex_;  // serializes every raw HID transfer: Write(), Read(), ReadAllBlocks()

  hid_device* device_ = nullptr;
  std::atomic<bool> write_error_logged_{false};
  std::atomic<bool> read_error_logged_{false};
  std::array<std::atomic<bool>, kMaxFigures> slot_present_{};
  mutable std::mutex detected_mutex_;
  std::array<std::optional<std::pair<uint16_t, uint16_t>>, kMaxFigures> slot_id_variant_;
};

}  // namespace giantrecomp::portal
