#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {
// Sentinel filename that should NEVER coexist with real assets, so a stale
// copy from a prior failed test run can't poison the result.
constexpr const char* kRel =
    "clips/gspeak/_test_asset_locator_resolve_for_read.gspeak";
}

TEST_CASE("resolveForRead returns source path when source file exists",
          "[unit][asset-locator]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty()) {
        WARN("no source-tree dev build detected; skipping");
        return;
    }
    REQUIRE_FALSE(src.empty());

    fs::create_directories(fs::path(src).parent_path());
    { std::ofstream out(src); out << "sentinel"; }

    const auto got = guitar_dsp::AssetLocator::resolveForRead(kRel);
    CHECK(got == src);

    fs::remove(src);
}

TEST_CASE("resolveForRead falls back to runtime when source file is absent",
          "[unit][asset-locator]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty()) {
        WARN("no source-tree dev build detected; skipping");
        return;
    }
    REQUIRE_FALSE(src.empty());

    // Make sure the source file is NOT present.
    fs::remove(src);

    const auto rt = guitar_dsp::AssetLocator::resolveRelativePath(kRel);
    const auto got = guitar_dsp::AssetLocator::resolveForRead(kRel);
    CHECK(got == rt);
}
