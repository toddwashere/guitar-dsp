#include <catch2/catch_test_macros.hpp>
#include "audio/TTSClip.h"

using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;

TEST_CASE("TTSClip: carries optional word segments", "[audio][ttsclip]") {
    TTSClip clip;
    clip.samples.assign(1000, 0.0f);
    REQUIRE(clip.words.empty());

    clip.words.push_back(WordSegment{"hello", 0, 400});
    clip.words.push_back(WordSegment{"world", 400, 1000});
    REQUIRE(clip.words.size() == 2);
    REQUIRE(clip.words[0].word == "hello");
    REQUIRE(clip.words[1].startSample == 400);
    REQUIRE(clip.words[1].endSample == 1000);
}
