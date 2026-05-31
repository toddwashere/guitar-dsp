#include <catch2/catch_test_macros.hpp>
#include <type_traits>
#include "audio/IVocoder.h"

TEST_CASE("IVocoder: header compiles + is abstract", "[audio][vocoder][smoke]") {
    static_assert(std::is_abstract_v<guitar_dsp::audio::IVocoder>,
                  "IVocoder must remain abstract");
    SUCCEED();
}
