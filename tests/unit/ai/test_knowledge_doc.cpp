#include <catch2/catch_test_macros.hpp>
#include "ai/KnowledgeDoc.h"
#include <juce_core/juce_core.h>
#include <thread>
#include <chrono>

using guitar_dsp::ai::KnowledgeDoc;

namespace {
juce::File makeTempDocWith(const juce::String& body) {
    auto f = juce::File::createTempFile("knowledge_doc_test.md");
    f.replaceWithText(body);
    return f;
}
} // namespace

TEST_CASE("KnowledgeDoc: returns initial contents", "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("hello world");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "hello world");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: empty string when file missing",
          "[ai][knowledge_doc]") {
    juce::File nope("/tmp/this-file-does-not-exist-xyz.md");
    if (nope.exists()) nope.deleteFile();
    KnowledgeDoc doc(nope);
    REQUIRE(doc.contents().empty());
}

TEST_CASE("KnowledgeDoc: re-reads after mtime change",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("first");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "first");
    // Filesystem mtime resolution is ~1s on macOS HFS+/APFS in some cases —
    // sleep enough to guarantee a distinct mtime.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    f.replaceWithText("second");
    REQUIRE(doc.contents() == "second");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: cached when mtime unchanged",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("cached body");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "cached body");
    // Calling again with no file change must still return same body.
    REQUIRE(doc.contents() == "cached body");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: cached when file legitimately empty",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("");
    KnowledgeDoc doc(f);
    REQUIRE(doc.contents() == "");
    // A truly cached load returns the same empty string without re-reading.
    // We can't easily prove no I/O happened, but we can verify the value
    // sticks and isn't replaced by something different on a subsequent call.
    REQUIRE(doc.contents() == "");
    f.deleteFile();
}

TEST_CASE("KnowledgeDoc: safe under concurrent contents() calls",
          "[ai][knowledge_doc]") {
    auto f = makeTempDocWith("concurrent body");
    KnowledgeDoc doc(f);
    std::atomic<int> errors{0};
    std::vector<std::thread> ts;
    for (int i = 0; i < 8; ++i) {
        ts.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                if (doc.contents() != "concurrent body") ++errors;
            }
        });
    }
    for (auto& t : ts) t.join();
    REQUIRE(errors.load() == 0);
    f.deleteFile();
}
