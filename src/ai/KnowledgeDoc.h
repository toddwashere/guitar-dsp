#pragma once

#include <juce_core/juce_core.h>

#include <mutex>
#include <string>

namespace guitar_dsp::ai {

// Hot-reloading text file loader. contents() stats the file and re-reads
// when mtime has changed since the last read. Returns "" when the file
// is missing or unreadable. Safe to call from multiple threads.
class KnowledgeDoc {
public:
    explicit KnowledgeDoc(juce::File path);

    std::string contents();

private:
    juce::File         path_;
    juce::Time         lastMtime_;
    std::string        cached_;
    bool               loaded_ = false;
    mutable std::mutex m_;
};

} // namespace guitar_dsp::ai
