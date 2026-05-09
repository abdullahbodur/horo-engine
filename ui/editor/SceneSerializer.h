/**
 * @file SceneSerializer.h
 * @brief JSON serialization and deserialization of SceneDocument to and from disk.
 */
#pragma once
#include <stdexcept>
#include <string>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    /** @brief Exception thrown by SceneSerializer when a load or save operation fails. */
    class SceneSerializerException : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /** @brief Reads and writes SceneDocument values to and from JSON files on disk. */
    class SceneSerializer {
    public:
        /**
         * @brief Loads a SceneDocument from a JSON file.
         * @param path Absolute or relative path to the scene JSON file.
         * @return The deserialized SceneDocument.
         * @throws SceneSerializerException if the path cannot be opened or the JSON is invalid.
         */
        static SceneDocument LoadFromFile(const std::string &path);

        /**
         * @brief Saves a SceneDocument to a JSON file.
         * @param doc  The document to serialize.
         * @param path Absolute or relative path to the output file.
         * @throws SceneSerializerException if the path cannot be written.
         */
        static void SaveToFile(const SceneDocument &doc, const std::string &path);
    };
} // namespace Horo::Editor
