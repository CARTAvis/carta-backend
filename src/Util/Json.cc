/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Json.h"
#include <regex>
#include "schemas/layout_schema_2.json.h"
#include "schemas/preference_backend_schema_2.json.h"
#include "schemas/preferences_schema_2.json.h"
#include "schemas/snippet_schema_1.json.h"
#include "schemas/workspace_schema_1.json.h"

using namespace carta;

const std::unordered_map<std::string, std::string_view> Json::_schema_strings{{"layout", CARTASCHEMA::layout_schema_2},
    {"preferences", CARTASCHEMA::preferences_schema_2}, {"snippet", CARTASCHEMA::snippet_schema_1},
    {"workspace", CARTASCHEMA::workspace_schema_1}, {"backend", CARTASCHEMA::preference_backend_schema_2}};

std::unordered_map<std::string, nlohmann::json> Json::_schemas{};
std::unordered_map<std::string, nlohmann::json_schema::json_validator> Json::_validators{};

nlohmann::json& Json::Schema(std::string object_type) {
    if (!_schema_strings.count(object_type)) {
        throw std::invalid_argument("Unrecognised object type.");
    }

    if (!_schemas.count(object_type)) {
        _schemas[object_type] = nlohmann::json::parse(_schema_strings.at(object_type));
    }

    return _schemas.at(object_type);
}

nlohmann::json_schema::json_validator& Json::Validator(std::string object_type) {
    auto schema = Schema(object_type);

    if (!_validators.count(object_type)) {
        _validators[object_type].set_root_schema(schema);
    }

    return _validators.at(object_type);
}
