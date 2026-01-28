/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "RegionImporter.h"

using namespace carta;

RegionImporter::RegionImporter(std::shared_ptr<casacore::CoordinateSystem> image_coord_sys, int file_id)
    : _coord_sys(image_coord_sys), _file_id(file_id) {}

std::vector<RegionProperties> RegionImporter::GetRegions(std::string& error) {
    // Parse the file in the constructor to create RegionProperties vector; return any errors in error
    error = _errors;
    if (_regions.size() == 0) {
        if (error.empty()) {
            error = "Import error: zero regions set. No regions defined or regions lie outside image coordinate system.";
        }
    }
    return _regions;
}

std::vector<std::string> RegionImporter::ReadRegionFile(const std::string& file, bool file_is_filename, const char extra_delim) {
    // Return file lines as string vector
    std::vector<std::string> file_lines;
    if (file_is_filename) {
        std::ifstream region_file;
        region_file.open(file);
        while (!region_file.eof()) {
            std::string single_line;
            getline(region_file, single_line);

            if (!single_line.empty() && (single_line.back() == '\r')) {
                // Remove carriage return from DOS file
                single_line.pop_back();
            }

            file_lines.push_back(single_line);
        }
        region_file.close();
    } else {
        std::string contents(file);
        SplitString(contents, '\n', file_lines);
    }

    if (extra_delim == '\0') {
        return file_lines;
    }

    // Split lines by delimiter
    std::vector<std::string> split_lines;
    for (auto& line : file_lines) {
        std::vector<std::string> compound_lines;
        SplitString(line, extra_delim, compound_lines);
        for (auto& single_line : compound_lines) {
            split_lines.push_back(single_line);
        }
    }
    return split_lines;
}

bool RegionImporter::IsCommentLine(const std::string& file_line) {
    // Determine if line starts with "# " + a region name.
    if (file_line[0] != '#') {
        return false;
    }
    if (file_line.find("# textbox") == 0 || file_line.find("# circle") == 0 || file_line.find("# segment") == 0) {
        // not in map, are type RECTANGLE (CRTF "centerbox", DS9 "box"), ELLIPSE ("ellipse"), and POLYLINE
        return false;
    }

    for (auto& region_name : _region_names) { // map {RegionType, name}
        if (file_line.find(region_name.second) == 0) {
            return false;
        }
    }

    return true;
}

void RegionImporter::ParseRegionParameters(
    std::string& region_definition, std::vector<std::string>& parameters, std::unordered_map<std::string, std::string>& properties) {
    // Parse the input string by space, comma, parentheses to get region parameters and properties (keyword=value)
    // Some annotation regions have comment syntax; remove leading #
    if (region_definition[0] == '#') {
        region_definition = region_definition.substr(2);
    }

    // Remove spaces around = to recognize properties
    std::regex equals_spaces("[ ]+=[ ]+");
    region_definition = std::regex_replace(region_definition, equals_spaces, "=");

    size_t next(0), current(0), end(region_definition.size());
    bool is_property(false), is_quoted_string(false);
    std::string property_key;

    while (current < end) {
        next = region_definition.find_first_of(_parser_delim, current);

        if (next == std::string::npos) {
            next = end;
        }

        if ((next - current) > 0) {
            // Item is region_definition between parser delimiters
            std::string item = region_definition.substr(current, next - current);

            if (!item.empty() && (item.front() == '"' || item.front() == '\'') && !(item.back() == '"' || item.back() == '\'')) {
                // find closing quote
                is_quoted_string = true;
                current = next;
                next = region_definition.find_first_of(item[0], current);
                item += region_definition.substr(current, next - current);
                item.erase(0, 1); // remove initial quote
            } else {
                is_quoted_string = false;
            }

            if (!is_quoted_string && item.find('=') != std::string::npos) {
                // Assume region property (kv pair)
                std::vector<std::string> kvpair;
                SplitString(item, '=', kvpair);
                property_key = kvpair[0];

                if (kvpair.size() == 1) {
                    // value starts with delim
                    current = next + 1;
                    next = region_definition.find_first_of(_parser_delim, current);
                    properties[property_key] = region_definition.substr(current, next - current);
                } else {
                    std::string value = kvpair[1];

                    if (!value.empty() && (value.front() == '"' || value.front() == '\'')) {
                        if (!value.empty() && (value.back() == '"' || value.back() == '\'')) {
                            value.pop_back(); // remove closing quote
                        } else {
                            // find closing quote
                            current = next;
                            next = region_definition.find_first_of(value[0], current);
                            value += region_definition.substr(current, next - current);
                        }
                        value.erase(0, 1); // remove initial quote
                    }
                    properties[property_key] = value;
                }
                is_property = true;
            } else {
                if (!is_property) {
                    parameters.push_back(item);
                } else {
                    properties[property_key] += " " + item;
                }
            }
        }

        if (next < end) {
            current = next + 1;
        } else {
            current = next;
        }
    }
}

std::string RegionImporter::GetProperty(
    const std::string& name, const std::unordered_map<std::string, std::string>& properties, bool check_global) {
    // Find property name in properties else global properties else return empty string
    if (properties.find(name) != properties.end()) {
        return properties.at(name);
    } else if (check_global && (_global_properties.find(name) != _global_properties.end())) {
        return _global_properties.at(name);
    }
    return "";
}

CARTA::TextAnnotationPosition RegionImporter::GetTextPosition(const std::string& position) {
    // Return position enum for string, or default CENTER
    CARTA::TextAnnotationPosition anno_position(CARTA::TextAnnotationPosition::CENTER);
    if (!position.empty()) {
        for (auto& text_position : text_positions) {
            if (text_position.second == position) {
                anno_position = text_position.first;
                break;
            }
        }
    }
    return anno_position;
}

void RegionImporter::AddTextStyleToProperties(const CARTA::RegionStyle& text_style, RegionProperties& textbox_properties) {
    // Add imported text style to existing textbox region properties.
    // Textbox defines state and style: name, text_position
    // Text defines style: color, text label, font
    textbox_properties.style.set_color(text_style.color());
    auto annotation_style = textbox_properties.style.mutable_annotation_style();
    annotation_style->set_text_label0(text_style.annotation_style().text_label0());
    annotation_style->set_font_style(text_style.annotation_style().font_style());
    annotation_style->set_font(text_style.annotation_style().font());
    annotation_style->set_font_size(text_style.annotation_style().font_size());
}

double RegionImporter::WorldToPixelLength(casacore::Quantity length, unsigned int pixel_axis) {
    // world->pixel conversion of ellipse/circle radius, box width/height, or compass length.
    // The opposite of casacore::CoordinateSystem::toWorldLength for pixel->world conversion.
    if (length.getUnit() == "pix") {
        return length.getValue();
    }

    // Convert to world axis units
    casacore::Vector<casacore::String> units = _coord_sys->worldAxisUnits();
    length.convert(units[pixel_axis]);

    // Find pixel length
    casacore::Vector<casacore::Double> increments(_coord_sys->increment());
    return fabs(length.getValue() / increments[pixel_axis]);
}

void RegionImporter::ImportCompassStyle(
    std::string& compass_properties, std::string& coordinate_system, CARTA::AnnotationStyle* annotation_style) {
    // Parse compass properties into AnnotationStyle fields
    std::vector<std::string> params;
    SplitString(compass_properties, ' ', params);

    if (params.size() == 5) { // compass=<coordinate system> <north label> <east label> [0|1] [0|1]
        coordinate_system = params[0];

        auto north_label = params[1];
        if (north_label.front() == '{' && north_label.back() == '}') {
            north_label = north_label.substr(1, north_label.length() - 2);
        }
        annotation_style->set_text_label0(north_label);

        auto east_label = params[2];
        if (east_label.front() == '{' && east_label.back() == '}') {
            east_label = east_label.substr(1, east_label.length() - 2);
        }
        annotation_style->set_text_label1(east_label);

        annotation_style->set_is_north_arrow(params[3] == "1" ? true : false);
        annotation_style->set_is_east_arrow(params[4] == "1" ? true : false);
    }
}

void RegionImporter::ImportRulerStyle(std::string& ruler_properties, std::string& coordinate_system) {
    // Parse ruler properties for coordinate system; unit unused in carta (frontend sets dynamically)
    std::vector<std::string> params;
    SplitString(ruler_properties, ' ', params);

    if (params.size() == 2) { // ruler=<coordinate system> [image|degrees|arcmin|arcsec]
        coordinate_system = params[0];
    }
}
