#pragma once

#include <juce_core/juce_core.h>

// Plugin-wide file logger. Backs onto juce::Logger so any code in any module
// can write a line via juce::Logger::writeToLog (which routes to the installed
// FileLogger). The helpers below prefix a severity tag and a timestamp so the
// resulting log file is grep-friendly.
//
// Log file lives at:
//   ~/Library/Logs/Guitar Speak/Guitar Speak <date>.log
//
// THREADING: All helpers acquire juce::FileLogger's internal mutex. Safe from
// the message thread and any worker thread. DO NOT call from the audio thread
// — file I/O on the RT path is verboten. Audio-thread code should publish to
// an atomic and let a message-thread timer drain it.

namespace guitar_dsp::log {

// Idempotent. Installs a date-stamped FileLogger as the current juce::Logger.
// Safe to call from any thread; the first call wins, subsequent calls are
// no-ops. Returns true on success.
bool init();

// The directory holding the active log file. Empty File if init() was never
// called or failed. Used by the DiagnosticPanel "reveal in Finder" affordance.
juce::File dir();

// The active log file. Empty File if init() was never called or failed.
juce::File file();

// Convenience wrappers. Each writes one line to the installed logger AND
// mirrors to stderr (so the standalone running in a terminal still shows
// them). The line format is:
//   [HH:MM:SS.mmm] [LEVEL] <msg>
void info (const juce::String& msg);
void warn (const juce::String& msg);
void error(const juce::String& msg);

} // namespace guitar_dsp::log
