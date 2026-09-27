#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
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
  //
  // `source`, if given, is remembered alongside `data` under the same lock, so a write callback
  // fired for this slot always sees the source that matches whichever figure is actually live at
  // that instant — even across a swap (PlaceFigure called again on an already-present slot) with
  // no separate, independently-lockable bookkeeping that could ever fall out of step with it.
  bool PlaceFigure(int slot, const FigureData& data,
                   std::optional<std::filesystem::path> source = std::nullopt);  // false if out of range
  bool RemoveFigure(int slot);  // false if out of range or empty; also forgets the source
  bool HasFigure(int slot) const;
  std::optional<FigureData> Figure(int slot) const;

  // Called after a successful figure write ('W' to a present slot and a valid block), with the
  // slot index, the figure's full data at that point, and the source PlaceFigure was given for
  // that slot (nullopt if none). Runs on the calling thread (whichever thread called Write()),
  // outside the portal's lock. Pass nullptr to remove it.
  void SetWriteCallback(
      std::function<void(int slot, const FigureData& data,
                         const std::optional<std::filesystem::path>& source)>
          callback);

 private:
  enum class SlotState : uint8_t { kEmpty = 0, kReady = 1, kRemoving = 2, kAdded = 3 };
  struct Slot {
    bool present = false;
    SlotState state = SlotState::kEmpty;
    int reports_left = 0;  // status reports still to show kAdded
    FigureData data{};
    std::optional<std::filesystem::path> source;
  };

  Report StatusReportLocked();

  mutable std::mutex mu_;
  std::deque<Report> replies_;
  std::array<Slot, kMaxFigures> slots_{};
  bool active_ = false;
  uint8_t counter_ = 0;
  std::function<void(int, const FigureData&, const std::optional<std::filesystem::path>&)> on_write_;
};

}  // namespace giantrecomp::portal
