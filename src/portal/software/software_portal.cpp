#include "portal/software/software_portal.h"

#include <algorithm>
#include <initializer_list>

namespace giantrecomp::portal {

namespace {

// A new figure shows as "added" for this many status reports before it settles to "ready".
constexpr int kAddedReports = 8;

Report MakeReport(std::initializer_list<uint8_t> bytes) {
  Report r{};
  std::copy(bytes.begin(), bytes.end(), r.begin());
  return r;
}

}  // namespace

void SoftwarePortal::Write(const Report& in) {
  std::lock_guard<std::mutex> lock(mu_);
  switch (in[0]) {
    case 'R':  // ready
      replies_.push_back(MakeReport({0x52, 0x02, 0x1B}));
      break;
    case 'A': {  // activate (argument != 0) or deactivate (argument == 0)
      const bool activate = in[1] != 0;
      if (activate && !active_) {
        // Figures are announced when the portal goes from inactive to active. The game repeats
        // 'A 01' about every 10 seconds while it is already active; announcing again then made
        // every figure look taken off and put straight back.
        for (Slot& s : slots_) {
          if (s.present) {
            s.state = SlotState::kAdded;
            s.reports_left = kAddedReports;
          }
        }
      } else if (!activate) {
        for (Slot& s : slots_) s.state = SlotState::kEmpty;  // figures stay; they are just not reported
      }
      active_ = activate;
      replies_.push_back(MakeReport({0x41, in[1], 0xFF, 0x77}));
      break;
    }
    case 'M':  // version
      replies_.push_back(MakeReport({0x4D, in[1], 0x00, 0x19}));
      break;
    case 'Q': {  // read one block of a figure
      const uint8_t slot = in[1] & 0x0F;
      const uint8_t block = in[2];
      Report out = MakeReport({0x51, slot, block});
      const Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        std::copy_n(s.data.begin() + block * kBlockSize, kBlockSize, out.begin() + 3);
      }
      replies_.push_back(out);
      break;
    }
    case 'W': {  // write one block of a figure
      const uint8_t slot = in[1] & 0x0F;
      const uint8_t block = in[2];
      Report out = MakeReport({0x57, slot, block});
      Slot& s = slots_[slot];
      if (s.present && block < kBlockCount) {
        out[1] |= 0x10;
        std::copy_n(in.begin() + 3, kBlockSize, s.data.begin() + block * kBlockSize);
      }
      replies_.push_back(out);
      break;
    }
    default:
      // 'S' and 'V' (status is answered by idle reads), the LED commands 'C', 'J' and 'L', and
      // anything unknown: no reply.
      break;
  }
}

Report SoftwarePortal::Read() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!replies_.empty()) {
    Report r = replies_.front();
    replies_.pop_front();
    return r;
  }
  return StatusReportLocked();
}

Report SoftwarePortal::StatusReportLocked() {
  uint32_t states = 0;
  if (active_) {
    for (int i = 0; i < kMaxFigures; ++i) {
      states |= static_cast<uint32_t>(slots_[i].state) << (2 * i);
    }
  }
  Report r = MakeReport({0x53, static_cast<uint8_t>(states), static_cast<uint8_t>(states >> 8),
                         static_cast<uint8_t>(states >> 16), static_cast<uint8_t>(states >> 24),
                         counter_++, static_cast<uint8_t>(active_ ? 1 : 0)});
  // Advance transitions once they have been reported.
  for (Slot& s : slots_) {
    if (s.state == SlotState::kAdded) {
      if (--s.reports_left <= 0) s.state = SlotState::kReady;
    } else if (s.state == SlotState::kRemoving) {
      s.state = SlotState::kEmpty;
    }
  }
  return r;
}

bool SoftwarePortal::PlaceFigure(int slot, const FigureData& data) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  Slot& s = slots_[slot];
  s.present = true;
  s.data = data;
  if (active_) {
    s.state = SlotState::kAdded;
    s.reports_left = kAddedReports;
  } else {
    s.state = SlotState::kEmpty;  // shown as added once the game activates the portal
  }
  return true;
}

bool SoftwarePortal::RemoveFigure(int slot) {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  Slot& s = slots_[slot];
  if (!s.present) return false;
  s.present = false;
  s.data = FigureData{};
  s.state = active_ ? SlotState::kRemoving : SlotState::kEmpty;
  return true;
}

bool SoftwarePortal::HasFigure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return false;
  std::lock_guard<std::mutex> lock(mu_);
  return slots_[slot].present;
}

std::optional<FigureData> SoftwarePortal::Figure(int slot) const {
  if (slot < 0 || slot >= kMaxFigures) return std::nullopt;
  std::lock_guard<std::mutex> lock(mu_);
  if (!slots_[slot].present) return std::nullopt;
  return slots_[slot].data;
}

}  // namespace giantrecomp::portal
