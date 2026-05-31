#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Scene.h"

namespace guitar_dsp::scenes {

class SceneLibrary {
public:
    // Loads one scene JSON file. Returns nullopt on parse failure, missing
    // required fields, or any I/O error.
    static std::optional<Scene> loadOne(const std::string& path);

    // Loads every *.json file in `directory`, returns scenes sorted by id.
    // Invalid files are skipped with a logged warning; loading continues.
    static std::vector<Scene> loadDirectory(const std::string& directory);
};

} // namespace guitar_dsp::scenes
