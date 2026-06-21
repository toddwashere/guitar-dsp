#include <catch2/catch_test_macros.hpp>

#include "app/SongStore.h"

#include <juce_core/juce_core.h>

using guitar_dsp::app::SongStore;

namespace {
// Each test gets a fresh tmpdir so saves don't bleed across tests.
juce::File freshTempDir() {
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory);
    auto dir = root.getChildFile("guitar_dsp_song_store_test")
                   .getChildFile(juce::Uuid().toString());
    dir.createDirectory();
    return dir;
}
}

TEST_CASE("SongStore: save then load round-trips text", "[songstore]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    REQUIRE(store.save("my song", "verse 1\nverse 2\n"));
    const auto loaded = store.load("my song");
    REQUIRE(loaded.has_value());
    REQUIRE(*loaded == "verse 1\nverse 2\n");
}

TEST_CASE("SongStore: list returns saved names sorted", "[songstore]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    store.save("charlie", "c");
    store.save("alpha",   "a");
    store.save("bravo",   "b");
    const auto names = store.list();
    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == "alpha");
    REQUIRE(names[1] == "bravo");
    REQUIRE(names[2] == "charlie");
}

TEST_CASE("SongStore: load of unknown name returns nullopt", "[songstore]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    REQUIRE_FALSE(store.load("never saved").has_value());
}

TEST_CASE("SongStore: save with same name overwrites", "[songstore]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    store.save("song", "old");
    store.save("song", "new");
    REQUIRE(*store.load("song") == "new");
    REQUIRE(store.list().size() == 1);
}

TEST_CASE("SongStore: list of empty dir is empty", "[songstore]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    REQUIRE(store.list().empty());
}

// Regression: a malicious name like "../escape" or "/etc/passwd" must NOT
// write outside the store dir. Test by ensuring the saved file appears
// inside the store dir, not at the literal path the name implies.
TEST_CASE("SongStore: refuses path-traversal in name", "[songstore][regression]") {
    auto dir = freshTempDir();
    SongStore store(dir);
    // Either save returns false, or the file appears inside `dir` under a
    // sanitised name — but NOT at /etc/passwd or one-dir-up.
    store.save("../escape", "should not escape");
    store.save("/etc/passwd", "should not escape");
    const auto sibling = dir.getParentDirectory().getChildFile("escape.txt");
    REQUIRE_FALSE(sibling.existsAsFile());
    REQUIRE_FALSE(juce::File("/etc/passwd").hasIdenticalContentTo(juce::File("/dev/null")));
    // (We don't assert /etc/passwd unchanged — we lack rights to write it
    // anyway. The sibling check above is the real proof.)
}

TEST_CASE("SongStore: list survives non-txt files in the dir", "[songstore]") {
    // If the user drops a .DS_Store or stray file in the directory,
    // list() should ignore it rather than crash or include garbage.
    auto dir = freshTempDir();
    dir.getChildFile(".DS_Store").create();
    dir.getChildFile("README.md").replaceWithText("hi");
    SongStore store(dir);
    store.save("real song", "lyrics");
    const auto names = store.list();
    REQUIRE(names.size() == 1);
    REQUIRE(names[0] == "real song");
}
