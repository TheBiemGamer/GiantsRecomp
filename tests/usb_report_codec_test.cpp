#include <cstring>

#include "portal/usb/usb_report_codec.h"
#include "test_util.h"

using namespace giantrecomp::portal;

int main() {
  // Encode: byte 0 is the mandatory HID report ID (0), then the 32 report bytes, then 32 bytes of
  // zero padding out to the device's real 64-byte report size.
  Report r{};
  r[0] = 0x51;
  r[1] = 0x02;
  r[31] = 0xEE;
  auto buf = EncodeOutputReport(r);
  CHECK(buf.size() == kDeviceWriteBufferSize);
  CHECK(buf[0] == 0x00);   // report ID
  CHECK(buf[1] == 0x51);   // report byte 0
  CHECK(buf[2] == 0x02);   // report byte 1
  CHECK(buf[32] == 0xEE);  // report byte 31 (last), at offset 1 (ID) + 31
  for (size_t i = 33; i < buf.size(); ++i) CHECK(buf[i] == 0x00);

  // Decode: a full 64-byte device report truncates to the first 32 bytes; anything past byte 32
  // is dropped, not carried into the 32-byte Report.
  uint8_t device_report[kDeviceReportSize] = {};
  device_report[0] = 0x53;
  device_report[40] = 0xAB;
  Report decoded = DecodeInputReport(device_report, sizeof(device_report));
  CHECK(decoded[0] == 0x53);
  CHECK(decoded[31] == 0x00);

  // Decode: a short read (fewer than 32 bytes available) zero-fills the rest of the Report.
  uint8_t short_report[5] = {0x52, 0x01, 0x02, 0x03, 0x04};
  Report short_decoded = DecodeInputReport(short_report, sizeof(short_report));
  CHECK(short_decoded[0] == 0x52);
  CHECK(short_decoded[4] == 0x04);
  CHECK(short_decoded[5] == 0x00);
  CHECK(short_decoded[31] == 0x00);

  // Decode: null/zero-length input is an all-zero Report, not a crash.
  Report empty_decoded = DecodeInputReport(nullptr, 0);
  for (uint8_t b : empty_decoded) CHECK(b == 0x00);

  return Finish("usb_report_codec");
}
