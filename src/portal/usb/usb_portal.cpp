#include "portal/usb/usb_portal.h"

#include "portal/usb/usb_report_codec.h"

namespace giantrecomp::portal {

UsbPortal::UsbPortal() {
  if (hid_init() != 0) return;
  for (const auto& [vendor_id, product_id] : kKnownPortals) {
    device_ = hid_open(vendor_id, product_id, nullptr);
    if (device_ != nullptr) break;
  }
  // Deliberately never call hid_exit(): it finalizes the whole hidapi library, not just this
  // device, and this portal lives for the whole process (see portal_hook.cpp's InstallConfiguredPortal,
  // which never frees its SoftwarePortal either, for the same reason). Process exit cleans this up.
}

UsbPortal::~UsbPortal() {
  if (device_ != nullptr) hid_close(device_);
}

void UsbPortal::Write(const Report& report) {
  if (device_ == nullptr) return;
  const auto buffer = EncodeOutputReport(report);
  hid_write(device_, buffer.data(), buffer.size());
}

Report UsbPortal::Read() {
  if (device_ == nullptr) return Report{};
  uint8_t buffer[kDeviceReportSize] = {};
  // A short timeout keeps this from blocking the game's polling thread indefinitely if the device
  // stops responding; an all-zero Report on timeout/failure is a shape the game already tolerates
  // (see docs/investigation/portal-protocol.md's status-frame/idle-read notes).
  const int bytes_read = hid_read_timeout(device_, buffer, sizeof(buffer), 50);
  if (bytes_read <= 0) return Report{};
  return DecodeInputReport(buffer, static_cast<size_t>(bytes_read));
}

}  // namespace giantrecomp::portal
