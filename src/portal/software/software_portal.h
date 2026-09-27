#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

#include "portal/portal_device.h"

namespace giantrecomp::portal {

// A portal implemented in software. Answers the game's command reports, holds up to 16 figures,
// and can be driven from another thread through PlaceFigure / RemoveFigure.
class SoftwarePortal : public PortalDevice {
 public:
  void Write(const Report& report) override;
  Report Read() override;

  // Control API. Thread-safe.
  bool PlaceFigure(int slot, const FigureData& data);  // false if `slot` is out of range
  bool RemoveFigure(int slot);                         // false if out of range or empty
  bool HasFigure(int slot) const;
  std::optional<FigureData> Figure(int slot) const;

 private:
  enum class SlotState : uint8_t { kEmpty = 0, kReady = 1, kRemoving = 2, kAdded = 3 };
  struct Slot {
    bool present = false;
    SlotState state = SlotState::kEmpty;
    int reports_left = 0;  // status reports still to show kAdded
    FigureData data{};
  };

  Report StatusReportLocked();

  mutable std::mutex mu_;
  std::deque<Report> replies_;
  std::array<Slot, kMaxFigures> slots_{};
  bool active_ = false;
  uint8_t counter_ = 0;
};

}  // namespace giantrecomp::portal
