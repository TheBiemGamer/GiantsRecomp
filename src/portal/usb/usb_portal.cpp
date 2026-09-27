#include "portal/usb/usb_portal.h"

#include <rex/logging.h>
#include <rex/string/utf8.h>

#include "portal/usb/usb_report_codec.h"

namespace giantrecomp::portal {

namespace {
// hid_error() returns a UTF-16 string on Windows (wchar_t and char16_t are the same width there,
// just distinct types) -- narrow it for REXLOG, which expects UTF-8.
std::string HidErrorUtf8(hid_device* device) {
  const wchar_t* message = hid_error(device);
  if (message == nullptr) return "(no error message)";
  return rex::string::to_utf8(
      std::u16string_view(reinterpret_cast<const char16_t*>(message)));
}
}  // namespace

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
  const int written = hid_write(device_, buffer.data(), buffer.size());
  // -1 is hid_write()'s only error indicator; anything else (including a short write) is not
  // treated as a failure here. Logged once, not every call, since this runs at the game's polling
  // rate and a stuck/unplugged device would otherwise flood the log.
  if (written < 0 && !write_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_write failed: {}", HidErrorUtf8(device_));
  }
}

Report UsbPortal::Read() {
  if (device_ == nullptr) return Report{};
  uint8_t buffer[kDeviceReportSize] = {};
  // A short timeout keeps this from blocking the game's polling thread indefinitely if the device
  // stops responding; an all-zero Report on timeout/failure is a shape the game already tolerates
  // (see docs/investigation/portal-protocol.md's status-frame/idle-read notes).
  const int bytes_read = hid_read_timeout(device_, buffer, sizeof(buffer), 50);
  // 0 means "no report within the timeout", hidapi's normal outcome for an idle poll, not a
  // failure. -1 is the actual error indicator. Logged once, for the same reason as Write() above.
  if (bytes_read < 0 && !read_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_read failed: {}", HidErrorUtf8(device_));
  }
  if (bytes_read <= 0) return Report{};
  return DecodeInputReport(buffer, static_cast<size_t>(bytes_read));
}

}  // namespace giantrecomp::portal
