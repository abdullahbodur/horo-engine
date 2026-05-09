/**
 * @file EditorSchema.h
 * @brief Schema definitions and loader for per-type editor UI field descriptors.
 */
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/editor/SceneDocument.h"

namespace Horo::Editor {
    /**
     * @brief Describes a single editable field in the properties panel UI.
     */
    struct FieldDef {
        /** @brief Controls the input widget rendered for this field. */
        enum class Widget { String, Float, Bool, Enum, Color3 };

        std::string key;               /**< Property map key used to read and write the field. */
        std::string label;             /**< Human-readable label displayed in the UI. */
        std::string description;       /**< Optional tooltip or description text. */
        Widget widget = Widget::String; /**< Widget type used to render this field. */
        bool hasDefault = false;       /**< True when a default value is provided. */
        bool required = false;         /**< True when the field must be non-empty. */
        bool allowEmpty = true;        /**< Whether an empty string value is accepted. */
        bool allowCustomValue = false; /**< Whether values not in @c options are permitted. */
        bool hasMin = false;           /**< True when a minimum numeric value is enforced. */
        bool hasMax = false;           /**< True when a maximum numeric value is enforced. */
        float minVal = 0.0f;           /**< Minimum allowed value (active when @c hasMin is true). */
        float maxVal = 1.0f;           /**< Maximum allowed value (active when @c hasMax is true). */
        std::vector<std::string> options; /**< Allowed option strings for Enum widgets. */
        std::string defaultValue;      /**< Default field value (active when @c hasDefault is true). */
    };

    /**
     * @brief Schema for a scene object type, listing the fields shown in the
     *        properties panel for objects of that type.
     */
    struct TypeSchema {
        std::string name;                   /**< Internal type name, e.g. "light". */
        std::string label;                  /**< Human-readable type label. */
        std::vector<std::string> appliesTo; /**< Scene object type names this schema applies to. */
        std::vector<FieldDef> fields;       /**< Ordered list of editable field descriptors. */
    };

    /**
     * @brief Schema for an attachable component, listing the fields shown when
     *        that component is selected in the properties panel.
     */
    struct ComponentSchema {
        std::string name;                   /**< Internal component type name, e.g. "rigidbody". */
        std::string label;                  /**< Human-readable component label. */
        std::vector<std::string> appliesTo; /**< Scene object type names this component can attach to. */
        std::vector<FieldDef> fields;       /**< Ordered list of editable field descriptors. */
    };

    /**
     * @brief Parses editor_schema.json and provides per-type field definitions
     *        consumed by the properties panel to build its UI.
     */
    class EditorSchema {
    public:
        /**
         * @brief Loads the schema from a JSON file at the given path.
         * @param path Path to the editor_schema.json file.
         * @note Silently no-ops on failure; the schema is optional.
         */
        void LoadFromFile(const std::string &path);

        /**
         * @brief Returns the TypeSchema for the given scene object type.
         * @param t The scene object type to look up.
         * @return Pointer to the TypeSchema, or nullptr if none is registered.
         */
        const TypeSchema *GetSchema(SceneObjectType t) const;

        /**
         * @brief Returns the TypeSchema for the given type name string.
         * @param typeName Internal type name, e.g. "light".
         * @return Pointer to the TypeSchema, or nullptr if none is registered.
         */
        const TypeSchema *GetSchemaByName(const std::string &typeName) const;

        /**
         * @brief Returns the ComponentSchema for the given component type name.
         * @param componentType Internal component type name, e.g. "rigidbody".
         * @return Pointer to the ComponentSchema, or nullptr if none is registered.
         */
        const ComponentSchema *
        GetComponentSchema(const std::string &componentType) const;

        /**
         * @brief Returns the full map of registered type schemas.
         * @return Const reference to the type name to TypeSchema map.
         */
        const std::unordered_map<std::string, TypeSchema, StringHash,
            std::equal_to<> > &
        TypeSchemas() const {
            return m_schemas;
        }

        /**
         * @brief Returns the full map of registered component schemas.
         * @return Const reference to the component name to ComponentSchema map.
         */
        const std::unordered_map<std::string, ComponentSchema, StringHash,
            std::equal_to<> > &
        ComponentSchemas() const {
            return m_componentSchemas;
        }

    private:
        std::unordered_map<std::string, TypeSchema, StringHash, std::equal_to<> >
        m_schemas; /**< Registered type schemas keyed by type name. */
        std::unordered_map<std::string, ComponentSchema, StringHash, std::equal_to<> >
        m_componentSchemas; /**< Registered component schemas keyed by component name. */
    };
} // namespace Horo::Editor
