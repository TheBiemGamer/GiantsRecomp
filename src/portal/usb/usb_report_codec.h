#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A conservative upper bound for the real portal's USB HID report size, oversized on purpose.
// PortalDevice::Report is 32 (matching the Xbox 360 console-side buffer the game itself uses --
// see xbox_frame.h). The command protocol is identical either way
// (docs/investigation/portal-protocol.md); only the size differs, and the real size is
// device-dependent -- verified 32 bytes both ways for the Wii U portal (1430:0150,
// docs/investigation/portal-protocol.md's "Real USB portal" section), unconfirmed for the
// whitelisted Xbox 360 variant (1430:1F17). Oversizing the write buffer relies on Windows' HID
// stack truncating hid_write() to the device's real OutputReportByteLength rather than failing;
// the read path handles any real size up to this bound correctly regardless, since
// DecodeInputReport works from hid_read()'s actual returned byte count, not this constant.
constexpr size_t kDeviceReportSize = 64;

// hid_write() always requires a leading HID report-ID byte before the report data, even for a
// device with a single, unnumbered report (ID 0) -- see hidapi's hid_write() documentation.
constexpr size_t kDeviceWriteBufferSize = kDeviceReportSize + 1;

// Builds the buffer to pass to hid_write(): byte 0 is the report ID (0), followed by `report`
// zero-padded from 32 to 64 bytes.
std::array<uint8_t, kDeviceWriteBufferSize> EncodeOutputReport(const Report& report);

// Converts a device input report (as returned by hid_read(), with no report-ID prefix for a
// single-report device) back to a 32-byte Report, truncating anything past the first kReportSize
// bytes. `data` may be null only if `length` is 0. Bytes beyond `length` (if length < kReportSize)
// are zero.
Report DecodeInputReport(const uint8_t* data, size_t length);

}  // namespace giantrecomp::portal
