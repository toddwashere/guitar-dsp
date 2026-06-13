#include "ai/ConversationBuffer.h"

namespace guitar_dsp::ai {

void ConversationBuffer::append(Message::Role role, std::string text) {
    std::lock_guard lk(mutex_);
    messages_.push_back({role, std::move(text)});
    if (messages_.size() > kMaxMessages)
        messages_.erase(messages_.begin(),
                        messages_.begin() + (messages_.size() - kMaxMessages));
}

void ConversationBuffer::clear() {
    std::lock_guard lk(mutex_);
    messages_.clear();
}

std::vector<Message> ConversationBuffer::snapshot() const {
    std::lock_guard lk(mutex_);
    return messages_;
}

} // namespace guitar_dsp::ai
