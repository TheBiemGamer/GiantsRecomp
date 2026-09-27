#include <initializer_list>

#include "portal/software/software_portal.h"
#include "test_util.h"

using namespace giantrecomp::portal;

static Report Cmd(std::initializer_list<uint8_t> bytes) {
  Report r{};
  size_t i = 0;
  for (uint8_t b : bytes) r[i++] = b;
  return r;
}

int main() {
  // Ready ('R') is answered with 52 02 1B, then idle reads are status reports.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R', 0x00}));
    Report reply = p.Read();
    CHECK(reply[0] == 0x52 && reply[1] == 0x02 && reply[2] == 0x1B);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x00);  // the active flag is clear before 'A'
  }

  // Activate ('A') echoes its argument: 41 <arg> FF 77. The status report then shows active.
  {
    SoftwarePortal p;
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'A', 0x01}));
    Report a0 = p.Read();
    Report a1 = p.Read();
    CHECK(a0[0] == 0x41 && a0[1] == 0x00 && a0[2] == 0xFF && a0[3] == 0x77);
    CHECK(a1[0] == 0x41 && a1[1] == 0x01 && a1[2] == 0xFF && a1[3] == 0x77);
    Report status = p.Read();
    CHECK(status[0] == 0x53);
    CHECK(status[6] == 0x01);
  }

  // Version ('M') echoes its argument: 4D <arg> 00 19.
  {
    SoftwarePortal p;
    p.Write(Cmd({'M', 0x01}));
    Report r = p.Read();
    CHECK(r[0] == 0x4D && r[1] == 0x01 && r[2] == 0x00 && r[3] == 0x19);
  }

  // Replies come back in the order the commands were sent.
  {
    SoftwarePortal p;
    p.Write(Cmd({'R'}));
    p.Write(Cmd({'A', 0x00}));
    p.Write(Cmd({'M', 0x01}));
    CHECK(p.Read()[0] == 0x52);
    CHECK(p.Read()[0] == 0x41);
    CHECK(p.Read()[0] == 0x4D);
    CHECK(p.Read()[0] == 0x53);  // then status
  }

  // Status ('S'), LED commands ('C', 'J', 'L') and unknown bytes queue no reply and do not crash.
  {
    SoftwarePortal p;
    p.Write(Cmd({'S'}));
    p.Write(Cmd({'V'}));
    p.Write(Cmd({'C', 0xE8, 0x10, 0x00}));
    p.Write(Cmd({'J', 0x01, 0xFF, 0x00, 0x00}));
    p.Write(Cmd({'L', 0x02, 0x00, 0xFF, 0x00}));
    p.Write(Cmd({0x00}));
    p.Write(Cmd({0xFF, 0xFF, 0xFF}));
    p.Write(Cmd({'z'}));
    CHECK(p.Read()[0] == 0x53);
    CHECK(p.Read()[0] == 0x53);
  }

  // The status counter is byte 5 and increments on every status report, wrapping at 256.
  {
    SoftwarePortal p;
    uint8_t previous = p.Read()[5];
    for (int i = 0; i < 300; ++i) {
      uint8_t next = p.Read()[5];
      CHECK(next == static_cast<uint8_t>(previous + 1));
      previous = next;
    }
  }

  return Finish("software_portal");
}
