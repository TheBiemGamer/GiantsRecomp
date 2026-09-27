#pragma once

#include <filesystem>
#include <string>

#include <rex/cvar.h>

namespace giantrecomp::portal {
class SoftwarePortal;
}  // namespace giantrecomp::portal

// So other translation units (the overlay) can read the folder the figure picker searches.
REXCVAR_DECLARE(std::string, portal_figures_dir);

namespace giantrecomp {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
void InstallConfiguredPortal();

// Loads the figure at `path` and places it in `slot`; future writes to that slot save back to
// `path`, replacing any earlier file that slot saved to. Returns false, and changes nothing, if
// there is no active software portal or `path` cannot be loaded as a figure.
bool PlaceFigureFromFile(int slot, const std::filesystem::path& path);

// Removes the figure from `slot`, if any, and forgets what file it was saving to. Returns false if
// there is no active software portal or the slot was already empty.
bool RemoveFigureFromSlot(int slot);

// The active software portal, for read-only status queries (HasFigure/Figure) from the overlay.
// nullptr if portal_mode is not "software".
portal::SoftwarePortal* GetSoftwarePortal();

}  // namespace giantrecomp
