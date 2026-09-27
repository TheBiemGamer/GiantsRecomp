#pragma once

#include <filesystem>
#include <string>

#include <rex/cvar.h>

#include "portal/figure_catalog.h"  // for portal::SkylanderInfo

namespace giantrecomp::portal {
class SoftwarePortal;
class UsbPortal;
}  // namespace giantrecomp::portal

// So other translation units (the overlay) can read the folder the figure picker searches.
REXCVAR_DECLARE(std::string, portal_figures_dir);

// So the overlay can distinguish "portal_mode is usb but no device was found" from "portal_mode
// isn't usb at all" -- both show GetUsbPortal() == nullptr, and those need different messages.
REXCVAR_DECLARE(std::string, portal_mode);

namespace giantrecomp {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
//
// If the `portal_figures_dir` cvar is empty, it is set to `default_figures_dir` (which is created
// if it doesn't exist yet) so the overlay has somewhere to look without the user passing a flag.
void InstallConfiguredPortal(const std::filesystem::path& default_figures_dir);

// Loads the figure at `path` and places it in `slot`; future writes to that slot save back to
// `path`, replacing any earlier file that slot saved to. Returns false, and changes nothing, if
// there is no active software portal or `path` cannot be loaded as a figure.
bool PlaceFigureFromFile(int slot, const std::filesystem::path& path);

// Removes the figure from `slot`, if any, and forgets what file it was saving to. Returns false if
// there is no active software portal or the slot was already empty.
bool RemoveFigureFromSlot(int slot);

// Creates a new blank figure for `sky` under portal_figures_dir/<game>/<name>.dump (creating the
// game subfolder if needed; an existing file of that name is never overwritten -- a " (2)", " (3)"
// suffix is added instead), then places it into `slot` exactly like PlaceFigureFromFile. Returns
// false, and creates nothing, if there is no active software portal, portal_figures_dir is unset,
// or the folder can't be created.
bool CreateAndPlaceFigure(int slot, const portal::SkylanderInfo& sky);

// The active software portal, for read-only status queries (HasFigure/Figure) from the overlay.
// nullptr if portal_mode is not "software".
portal::SoftwarePortal* GetSoftwarePortal();

// The active USB portal, for read-only status queries (FigurePresent/DetectedIdVariant) from the
// overlay. nullptr if portal_mode is not "usb" or no device was found.
portal::UsbPortal* GetUsbPortal();

}  // namespace giantrecomp
