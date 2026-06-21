#include "ai/KnowledgeDoc.h"

namespace guitar_dsp::ai {

KnowledgeDoc::KnowledgeDoc(juce::File path)
    : path_(std::move(path)) {}

std::string KnowledgeDoc::contents() {
    std::lock_guard<std::mutex> lk(m_);

    if (!path_.existsAsFile()) {
        cached_.clear();
        lastMtime_ = juce::Time{};
        return {};
    }

    const auto mtime = path_.getLastModificationTime();
    if (mtime == lastMtime_ && !cached_.empty()) return cached_;

    cached_     = path_.loadFileAsString().toStdString();
    lastMtime_  = mtime;
    return cached_;
}

} // namespace guitar_dsp::ai
