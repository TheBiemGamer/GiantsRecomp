#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

#include <libusb.h>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A small set of VID/PID pairs known to be a real Skylanders Portal of Power, matching Cemu's own
// nsyshid whitelist for this device family (read for this fact only, not copied code).
inline constexpr std::pair<uint16_t, uint16_t> kKnownPortals[] = {
    {0x1430, 0x0150},  // Wii U Skylanders portal
    {0x1430, 0x1F17},  // Xbox 360 Skylanders portal
};

// A real, physical Portal of Power, opened over raw USB via libusb -- the same transport Cemu's
// own real-hardware backend (nsyshid BackendLibusb) uses, bypassing Windows' HID class driver
// entirely. Requires the device's driver to be rebound to WinUSB (e.g. via Zadig); unlike the
// earlier hidapi-based attempt, this is a hard requirement here, not an optional convenience.
//
// Sends PortalDevice::Report (32 bytes) via a HID SET_REPORT control transfer (see Write()) and
// receives it on the device's discovered interrupt IN endpoint, with no report-ID prefix or
// padding on either side -- unlike the HID class layer, raw USB has no such concept, and the real
// device's report size was verified to be exactly 32 bytes both ways
// (docs/investigation/portal-protocol.md). The command protocol itself needs no translation
// (docs/investigation/portal-protocol.md).
class UsbPortal final : public PortalDevice {
 public:
  // Opens the first connected device matching kKnownPortals, in the order listed. Check IsOpen()
  // before installing this as the active portal -- construction never throws or logs; the caller
  // decides what to do if no matching device was found.
  UsbPortal();
  ~UsbPortal() override;

  UsbPortal(const UsbPortal&) = delete;
  UsbPortal& operator=(const UsbPortal&) = delete;

  bool IsOpen() const { return handle_ != nullptr; }

  void Write(const Report& report) override;
  Report Read() override;

 private:
  // Finds the first IN endpoint on the device's active configuration (loosely matching Cemu's
  // DeviceLibusb::FindDefaultDeviceEndpoints, minus the OUT side -- writes go through a control
  // transfer, see Write()), and claims the interface it belongs to. Returns false, leaving
  // handle_ open but unusable, if none is found.
  bool ClaimInterfaceAndFindEndpoints(libusb_device* device);

  libusb_context* ctx_ = nullptr;
  libusb_device_handle* handle_ = nullptr;
  uint8_t endpoint_in_ = 0;
  std::atomic<bool> write_error_logged_{false};
  std::atomic<bool> read_error_logged_{false};
};

}  // namespace giantrecomp::portal
