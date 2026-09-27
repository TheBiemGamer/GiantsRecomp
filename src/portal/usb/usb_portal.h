#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>

#include <hidapi.h>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A small set of VID/PID pairs known to be a real Skylanders Portal of Power, matching Cemu's own
// nsyshid whitelist for this device family (read for this fact only, not copied code).
inline constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal
};

// A real, physical Portal of Power, opened over USB HID via hidapi -- no WinUSB/Zadig driver
// replacement needed, unlike the libusb-based version this replaced (see
// docs/investigation/portal-protocol.md for why that attempt needed it and this one doesn't:
// same underlying transfer, hid_send_output_report() reaches it through the stock HID class
// driver instead of bypassing it).
//
// Commands go out via hid_send_output_report() (a HID SET_REPORT control transfer) -- not
// hid_write(), which prefers this device's interrupt OUT endpoint and is silently ignored by its
// firmware. Reads use plain hid_read_timeout(); the real device's report size was verified to be
// exactly 32 bytes both ways (docs/investigation/portal-protocol.md), matching
// PortalDevice::Report directly.
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

 private:
  hid_device* device_ = nullptr;
  std::atomic<bool> write_error_logged_{false};
  std::atomic<bool> read_error_logged_{false};
};

}  // namespace giantrecomp::portal
