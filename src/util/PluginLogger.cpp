#include "PluginLogger.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>

namespace guitar_dsp::log {
namespace {

std::once_flag g_initFlag;
std::atomic<bool> g_initialized {false};

// Owns the FileLogger that's installed as juce::Logger::getCurrentLogger().
// Held via unique_ptr so it's destroyed at process exit (JUCE's Logger
// interface doesn't own its current logger). Reset in init() before assigning
// — JUCE's setCurrentLogger doesn't take ownership.
std::unique_ptr<juce::FileLogger> g_fileLogger;
juce::File g_logFile;

juce::String prefix(const char* level) {
    return "[" + juce::Time::getCurrentTime().formatted("%H:%M:%S")
         + "." + juce::String(juce::Time::getCurrentTime().getMilliseconds()).paddedLeft('0', 3)
         + "] [" + level + "] ";
}

void writeLine(const char* level, const juce::String& msg) {
    const auto line = prefix(level) + msg;
    // juce::Logger::writeToLog is a no-op if no current logger is installed,
    // so calls before init() simply hit stderr via the mirror below.
    juce::Logger::writeToLog(line);
    std::fprintf(stderr, "%s\n", line.toRawUTF8());
}

} // namespace

bool init() {
    std::call_once(g_initFlag, []{
        // FileLogger::createDateStampedLogger picks the platform's user log
        // directory (~/Library/Logs on macOS) and creates the sub-dir if
        // needed. The welcome message is written as the first line.
        auto* fl = juce::FileLogger::createDateStampedLogger(
            /*subDirectory=*/ "Guitar Speak",
            /*nameRoot=*/    "Guitar Speak ",
            /*suffix=*/      ".log",
            /*welcome=*/     "----- Guitar Speak log opened -----");
        if (fl == nullptr) return;
        g_fileLogger.reset(fl);
        g_logFile = fl->getLogFile();
        juce::Logger::setCurrentLogger(g_fileLogger.get());
        g_initialized.store(true, std::memory_order_release);
    });
    return g_initialized.load(std::memory_order_acquire);
}

juce::File dir() {
    return g_initialized.load(std::memory_order_acquire)
        ? g_logFile.getParentDirectory()
        : juce::File{};
}

juce::File file() {
    return g_initialized.load(std::memory_order_acquire) ? g_logFile : juce::File{};
}

void info (const juce::String& msg) { writeLine("INFO",  msg); }
void warn (const juce::String& msg) { writeLine("WARN",  msg); }
void error(const juce::String& msg) { writeLine("ERROR", msg); }

} // namespace guitar_dsp::log
