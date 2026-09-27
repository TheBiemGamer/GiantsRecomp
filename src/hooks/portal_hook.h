#pragma once

namespace giantrecomp {

// Creates the portal selected by the `portal_mode` cvar and routes the game's portal reads and
// writes to it. With no portal (mode `none` or an unknown value) the game keeps its own path and
// shows "Can't find the Portal of Power". The portal lives for the whole process, because game
// threads may still call into it while the app shuts down.
void InstallConfiguredPortal();

}  // namespace giantrecomp
