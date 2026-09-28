#pragma once

namespace giantsrecomp {

// Resolves the `language` cvar (a friendly name, or a raw numeric Xbox 360 language ID for
// experimentation) and applies it to the `user_language` cvar that XGetLanguage/
// ExGetXConfigSetting actually read. Call once at startup, before the game can query either.
void ApplyLanguageSetting();

}  // namespace giantsrecomp
