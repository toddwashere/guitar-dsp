#pragma once

#include <string>

namespace guitar_dsp {

// Returns the directory containing scene/MIDI/etc. assets at runtime.
// Precedence:
//   1. $GUITAR_DSP_ASSETS_DIR (if set and exists)
//   2. <app bundle>/Contents/Resources/assets/
//   3. The repo's `assets/` directory walked from CWD (for unit tests)
class AssetLocator {
public:
    static std::string scenesDirectory();
    static std::string midiDirectory();
    static std::string ttsDirectory();

private:
    static std::string assetsRoot();
};

} // namespace guitar_dsp
