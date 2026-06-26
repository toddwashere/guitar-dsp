#include <catch2/catch_test_macros.hpp>
#include "audio/LockFreeSPSCRing.h"

#include <array>
#include <atomic>
#include <thread>
#include <vector>

using guitar_dsp::audio::LockFreeSPSCRing;

TEST_CASE("LockFreeSPSCRing: read on empty returns 0", "[audio][ring]") {
    LockFreeSPSCRing<float> r(64);
    float out[8] = {};
    REQUIRE(r.read(out, 8) == 0);
    REQUIRE(r.available() == 0);
}

TEST_CASE("LockFreeSPSCRing: write into empty then read back", "[audio][ring]") {
    LockFreeSPSCRing<float> r(64);
    const float in[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    REQUIRE(r.write(in, 4) == 4);
    REQUIRE(r.available() == 4);
    float out[4] = {};
    REQUIRE(r.read(out, 4) == 4);
    REQUIRE(out[0] == 1.0f);
    REQUIRE(out[3] == 4.0f);
    REQUIRE(r.available() == 0);
}

TEST_CASE("LockFreeSPSCRing: write into full returns short count", "[audio][ring]") {
    LockFreeSPSCRing<float> r(8);
    std::array<float, 16> in{};
    for (int i = 0; i < 16; ++i) in[i] = float(i);
    const std::size_t written = r.write(in.data(), 16);
    REQUIRE(written <= 8);
    REQUIRE(r.free_space() == 8 - written);
}

TEST_CASE("LockFreeSPSCRing: wrap-around preserves order", "[audio][ring]") {
    LockFreeSPSCRing<float> r(8);
    std::array<float, 6> a{1, 2, 3, 4, 5, 6};
    REQUIRE(r.write(a.data(), 6) == 6);
    float drain[4]; REQUIRE(r.read(drain, 4) == 4);
    std::array<float, 4> b{7, 8, 9, 10};
    REQUIRE(r.write(b.data(), 4) == 4);
    float out[6]; REQUIRE(r.read(out, 6) == 6);
    REQUIRE(out[0] == 5.0f);
    REQUIRE(out[1] == 6.0f);
    REQUIRE(out[2] == 7.0f);
    REQUIRE(out[5] == 10.0f);
}

TEST_CASE("LockFreeSPSCRing: stress producer/consumer threads", "[audio][ring][stress]") {
    constexpr std::size_t N = 100'000;
    LockFreeSPSCRing<int> r(1024);
    std::vector<int> got;
    got.reserve(N);

    std::thread consumer([&]{
        int buf[64];
        while (got.size() < N) {
            const auto n = r.read(buf, 64);
            for (std::size_t i = 0; i < n; ++i) got.push_back(buf[i]);
        }
    });

    for (int i = 0; i < int(N); ) {
        int chunk[32];
        int j = 0;
        for (; j < 32 && i < int(N); ++j, ++i) chunk[j] = i;
        std::size_t off = 0;
        while (off < std::size_t(j)) {
            off += r.write(chunk + off, std::size_t(j) - off);
        }
    }
    consumer.join();

    REQUIRE(got.size() == N);
    for (std::size_t i = 0; i < N; ++i) REQUIRE(got[i] == int(i));
}
