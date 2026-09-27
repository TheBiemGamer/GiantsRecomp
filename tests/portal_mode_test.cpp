#include "portal/portal_mode.h"
#include "test_util.h"

using giantrecomp::portal::ParsePortalMode;
using giantrecomp::portal::PortalMode;

int main() {
  CHECK(ParsePortalMode("software") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("none") == PortalMode::kNone);
  CHECK(ParsePortalMode("  Software \t") == PortalMode::kSoftware);
  CHECK(ParsePortalMode("NONE") == PortalMode::kNone);

  // Unknown values are rejected, including modes that are not implemented yet.
  CHECK(!ParsePortalMode("usb").has_value());
  CHECK(!ParsePortalMode("").has_value());
  CHECK(!ParsePortalMode("   ").has_value());
  CHECK(!ParsePortalMode("banana").has_value());
  CHECK(!ParsePortalMode("software extra").has_value());

  return Finish("portal_mode");
}
