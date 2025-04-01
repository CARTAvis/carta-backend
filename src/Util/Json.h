/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#ifndef CARTA_SRC_UTIL_JSON_H_
#define CARTA_SRC_UTIL_JSON_H_

#include <unordered_set>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

namespace carta {
class Json {
public:
    static nlohmann::json& Schema(std::string object_type);
    static nlohmann::json_schema::json_validator& Validator(std::string object_type);

private:
    static const std::unordered_map<std::string, std::string_view> _schema_strings;
    static std::unordered_map<std::string, nlohmann::json> _schemas;
    static std::unordered_map<std::string, nlohmann::json_schema::json_validator> _validators;
};
} // namespace carta

#endif // CARTA_SRC_UTIL_JSON_H_
