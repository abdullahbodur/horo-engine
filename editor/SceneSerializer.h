#pragma once
#include <stdexcept>
#include <string>

#include "editor/SceneDocument.h"

namespace Monolith::Editor {
    class SceneSerializerException : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    class SceneSerializer {
    public:
        // Throws SceneSerializerException if path cannot be opened or JSON is
        // invalid.
        static SceneDocument LoadFromFile(const std::string &path);

        // Throws SceneSerializerException if path cannot be written.
        static void SaveToFile(const SceneDocument &doc, const std::string &path);
    };
} // namespace Monolith::Editor
