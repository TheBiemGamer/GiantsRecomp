#include "portal/usb/usb_report_codec.h"

#include <algorithm>
#include <cstring>

namespace giantrecomp::portal {

std::array<uint8_t, kDeviceWriteBufferSize> EncodeOutputReport(const Report& report) {
  std::array<uint8_t, kDeviceWriteBufferSize> buffer{};  // buffer[0] = report ID 0, rest zero
  std::copy(report.begin(), report.end(), buffer.begin() + 1);
  return buffer;
}

Report DecodeInputReport(const uint8_t* data, size_t length) {
  Report report{};
  const size_t to_copy = std::min(length, report.size());
  if (data != nullptr && to_copy > 0) {
    std::memcpy(report.data(), data, to_copy);
  }
  return report;
}

}  // namespace giantrecomp::portal
