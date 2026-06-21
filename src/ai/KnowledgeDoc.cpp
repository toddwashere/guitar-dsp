#include "ai/KnowledgeDoc.h"

namespace guitar_dsp::ai {

KnowledgeDoc::KnowledgeDoc(juce::File path)
    : path_(std::move(path)) {}

std::string KnowledgeDoc::contents() {
    std::lock_guard<std::mutex> lk(m_);

    if (!path_.existsAsFile()) {
        cached_.clear();
        lastMtime_ = juce::Time{};
        loaded_    = false;
        return {};
    }

    const auto mtime = path_.getLastModificationTime();
    if (loaded_ && mtime == lastMtime_) return cached_;

    cached_     = path_.loadFileAsString().toStdString();
    lastMtime_  = mtime;
    loaded_     = true;
    return cached_;
}

} // namespace guitar_dsp::ai
