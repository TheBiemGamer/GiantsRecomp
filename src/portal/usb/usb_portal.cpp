#include "portal/usb/usb_portal.h"

#include <rex/logging.h>

namespace giantrecomp::portal {

namespace {

bool IsKnownPortal(const libusb_device_descriptor& desc) {
  for (const auto& [vendor_id, product_id] : kKnownPortals) {
    if (desc.idVendor == vendor_id && desc.idProduct == product_id) return true;
  }
  return false;
}

}  // namespace

UsbPortal::UsbPortal() {
  if (libusb_init(&ctx_) != LIBUSB_SUCCESS) {
    ctx_ = nullptr;
    return;
  }

  libusb_device** devices = nullptr;
  const ssize_t count = libusb_get_device_list(ctx_, &devices);
  if (count < 0) return;

  for (ssize_t i = 0; i < count && handle_ == nullptr; ++i) {
    libusb_device* device = devices[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(device, &desc) != LIBUSB_SUCCESS) continue;
    if (!IsKnownPortal(desc)) continue;

    libusb_device_handle* handle = nullptr;
    if (libusb_open(device, &handle) != LIBUSB_SUCCESS) continue;
    handle_ = handle;

    if (!ClaimInterfaceAndFindEndpoints(device)) {
      libusb_close(handle_);
      handle_ = nullptr;
      continue;
    }
  }
  libusb_free_device_list(devices, 1);
}

bool UsbPortal::ClaimInterfaceAndFindEndpoints(libusb_device* device) {
  libusb_config_descriptor* config = nullptr;
  if (libusb_get_active_config_descriptor(device, &config) != LIBUSB_SUCCESS) return false;

  // Only the IN endpoint is required: commands go out through a HID SET_REPORT control transfer
  // (see Write()), not an interrupt OUT report -- matching how this device's firmware actually
  // expects to receive them (verified against real hardware; see
  // docs/investigation/portal-protocol.md). An OUT endpoint, if the descriptor declares one, is
  // simply unused.
  bool found_in = false;
  uint8_t claimed_interface = 0;
  for (uint8_t i = 0; i < config->bNumInterfaces && !found_in; ++i) {
    const libusb_interface& interface = config->interface[i];
    for (int alt = 0; alt < interface.num_altsetting && !found_in; ++alt) {
      const libusb_interface_descriptor& altsetting = interface.altsetting[alt];
      for (uint8_t e = 0; e < altsetting.bNumEndpoints; ++e) {
        const libusb_endpoint_descriptor& endpoint = altsetting.endpoint[e];
        if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0) {
          endpoint_in_ = endpoint.bEndpointAddress;
          found_in = true;
          claimed_interface = altsetting.bInterfaceNumber;
          break;
        }
      }
    }
  }
  libusb_free_config_descriptor(config);

  if (!found_in) return false;
  return libusb_claim_interface(handle_, claimed_interface) == LIBUSB_SUCCESS;
}

UsbPortal::~UsbPortal() {
  if (handle_ != nullptr) libusb_close(handle_);
  if (ctx_ != nullptr) libusb_exit(ctx_);
}

void UsbPortal::Write(const Report& report) {
  if (handle_ == nullptr) return;
  // Commands go out via a HID SET_REPORT control transfer (Output report, ID 0), not the
  // interrupt OUT endpoint -- matching Cemu's own real-hardware backend (nsyshid
  // DeviceLibusb::SetReport). Verified against real hardware: a plain interrupt-OUT write is
  // accepted at the transport level (rc=0) but silently ignored by the firmware -- SET_REPORT is
  // the only way that gets a real, distinct reply out of the device (see
  // docs/investigation/portal-protocol.md).
  constexpr uint8_t kHidSetReport = 0x09;
  constexpr uint16_t kOutputReportType = 0x02;
  const uint16_t report_type_and_id = (kOutputReportType << 8) | 0x00;
  constexpr uint8_t kRequestType =
      static_cast<uint8_t>(LIBUSB_REQUEST_TYPE_CLASS) | static_cast<uint8_t>(LIBUSB_RECIPIENT_INTERFACE) |
      static_cast<uint8_t>(LIBUSB_ENDPOINT_OUT);
  const int rc = libusb_control_transfer(
      handle_, kRequestType, kHidSetReport, report_type_and_id, 0,
      const_cast<uint8_t*>(report.data()),
      static_cast<uint16_t>(report.size()), 50);
  // libusb_control_transfer returns the number of bytes transferred (>= 0) on success, or a
  // negative libusb_error code on failure -- unlike libusb_interrupt_transfer's rc/actual split.
  if (rc < 0 && rc != LIBUSB_ERROR_TIMEOUT && !write_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): libusb write failed: {}", libusb_error_name(rc));
  }
}

Report UsbPortal::Read() {
  if (handle_ == nullptr) return Report{};
  Report report{};
  int actual = 0;
  const int rc = libusb_interrupt_transfer(handle_, endpoint_in_, report.data(),
                                           static_cast<int>(report.size()), &actual, 50);
  if (rc != LIBUSB_SUCCESS) {
    if (rc != LIBUSB_ERROR_TIMEOUT && !read_error_logged_.exchange(true)) {
      REXLOG_WARN("Portal (usb): libusb read failed: {}", libusb_error_name(rc));
    }
    return Report{};
  }
  return report;
}

}  // namespace giantrecomp::portal
