#pragma once

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

// A real, physical Portal of Power, opened over USB HID via hidapi. Relays the game's 32-byte
// reports to and from the device's real 64-byte reports (see usb_report_codec.h) -- the command
// protocol itself needs no translation (docs/investigation/portal-protocol.md).
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
};

}  // namespace giantrecomp::portal
