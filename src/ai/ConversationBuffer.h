#pragma once
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

struct Message {
    enum class Role { System, User, Assistant };
    Role        role;
    std::string text;
};

class ConversationBuffer {
public:
    static constexpr std::size_t kMaxMessages = 10;

    void                 append(Message::Role role, std::string text);
    void                 clear();
    std::vector<Message> snapshot() const;

private:
    mutable std::mutex   mutex_;
    std::vector<Message> messages_;
};

} // namespace guitar_dsp::ai
