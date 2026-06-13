#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationBuffer.h"

using guitar_dsp::ai::ConversationBuffer;
using guitar_dsp::ai::Message;

TEST_CASE("ConversationBuffer: append then snapshot returns messages in order",
          "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "hi");
    b.append(Message::Role::Assistant, "hello");
    auto s = b.snapshot();
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].role == Message::Role::User);
    REQUIRE(s[0].text == "hi");
    REQUIRE(s[1].role == Message::Role::Assistant);
    REQUIRE(s[1].text == "hello");
}

TEST_CASE("ConversationBuffer: truncates to last 10 messages",
          "[ai][buffer]") {
    ConversationBuffer b;
    for (int i = 0; i < 12; ++i)
        b.append(Message::Role::User, std::to_string(i));
    auto s = b.snapshot();
    REQUIRE(s.size() == 10);
    REQUIRE(s.front().text == "2");
    REQUIRE(s.back().text == "11");
}

TEST_CASE("ConversationBuffer: clear empties", "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "hi");
    b.clear();
    REQUIRE(b.snapshot().empty());
}

TEST_CASE("ConversationBuffer: snapshot survives subsequent mutations",
          "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "first");
    auto s = b.snapshot();
    b.append(Message::Role::Assistant, "second");
    REQUIRE(s.size() == 1);
    REQUIRE(s[0].text == "first");
}
