#include "portal/usb/usb_portal.h"

#include <algorithm>

#include <rex/logging.h>
#include <rex/string/utf8.h>

namespace giantrecomp::portal {

namespace {

// hid_error() returns a UTF-16 string on Windows (wchar_t and char16_t are the same width there,
// just distinct types) -- narrow it for REXLOG, which expects UTF-8.
std::string HidErrorUtf8(hid_device* device) {
  const wchar_t* message = hid_error(device);
  if (message == nullptr) return "(no error message)";
  return rex::string::to_utf8(std::u16string_view(reinterpret_cast<const char16_t*>(message)));
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
  // hid_send_output_report() sends via a HID SET_REPORT control transfer -- unlike hid_write(),
  // which prefers this device's interrupt OUT endpoint and is accepted at the transport level but
  // silently discarded by its firmware. Needs a leading report-ID byte (0, this device has no
  // numbered reports), so the buffer is one longer than the report itself.
  std::array<uint8_t, kReportSize + 1> buffer{};  // buffer[0] = report ID 0
  std::copy(report.begin(), report.end(), buffer.begin() + 1);
  const int written = hid_send_output_report(device_, buffer.data(), buffer.size());
  if (written < 0 && !write_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_send_output_report failed: {}", HidErrorUtf8(device_));
  }
}

Report UsbPortal::Read() {
  if (device_ == nullptr) return Report{};
  Report report{};
  // A short timeout keeps this from blocking the game's polling thread indefinitely if the device
  // stops responding; an all-zero Report on timeout/failure is a shape the game already tolerates
  // (docs/portal-protocol.md).
  const int bytes_read = hid_read_timeout(device_, report.data(), report.size(), 50);
  // 0 means "no report within the timeout", hidapi's normal outcome for an idle poll, not a
  // failure. -1 is the actual error indicator.
  if (bytes_read < 0 && !read_error_logged_.exchange(true)) {
    REXLOG_WARN("Portal (usb): hid_read failed: {}", HidErrorUtf8(device_));
  }
  if (bytes_read <= 0) return Report{};
  ObserveReply(report);
  return report;
}

bool UsbPortal::FigurePresent() const {
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (slot_present_[slot].load()) return true;
  }
  return false;
}

std::vector<int> UsbPortal::PresentSlots() const {
  std::vector<int> slots;
  for (int slot = 0; slot < kMaxFigures; ++slot) {
    if (slot_present_[slot].load()) slots.push_back(slot);
  }
  return slots;
}

std::optional<std::pair<uint16_t, uint16_t>> UsbPortal::DetectedIdVariant(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(detected_mutex_);
  return slot_id_variant_[slot];
}

void UsbPortal::ObserveReply(const Report& report) {
  // 'S' status frame: 0x53, then 4 bytes of little-endian slot state (2 bits each, slot 0
  // lowest -- up to kMaxFigures slots, matching a real portal holding more than one figure at
  // once, e.g. Giants' 2-player co-op plus items), a counter, and an active flag
  // (docs/portal-protocol.md).
  if (report[0] == 0x53) {
    const uint32_t states = static_cast<uint32_t>(report[1]) | (static_cast<uint32_t>(report[2]) << 8) |
                            (static_cast<uint32_t>(report[3]) << 16) |
                            (static_cast<uint32_t>(report[4]) << 24);
    for (int slot = 0; slot < kMaxFigures; ++slot) {
      const bool present = ((states >> (2 * slot)) & 0x03) != 0;
      slot_present_[slot].store(present);
      if (!present) {
        std::lock_guard<std::mutex> lock(detected_mutex_);
        slot_id_variant_[slot].reset();
      }
    }
    return;
  }
  // 'Q' reply to a block-1 read: 0x51, slot (low nibble) with 0x10 set if present, block index,
  // then the block's 16 data bytes. Block 1 covers global figure offsets 0x10-0x1F, where id
  // (offset 0x10) and variant (offset 0x1C) live -- see figure_file.h's
  // ReadFigureId/ReadFigureVariant, which read the same two fields from a full 1024-byte dump.
  if (report[0] == 0x51 && (report[1] & 0x10) != 0 && report[2] == 1) {
    const int slot = report[1] & 0x0F;  // always 0-15, matching kMaxFigures
    const uint16_t id = static_cast<uint16_t>(report[3]) | (static_cast<uint16_t>(report[4]) << 8);
    const uint16_t variant =
        static_cast<uint16_t>(report[15]) | (static_cast<uint16_t>(report[16]) << 8);
    std::lock_guard<std::mutex> lock(detected_mutex_);
    slot_id_variant_[slot] = std::make_pair(id, variant);
  }
}

}  // namespace giantrecomp::portal
