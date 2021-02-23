/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "Logger.h"

#ifdef _BOOST_FILESYSTEM_
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

void InitLogger(bool no_log_file, int verbosity) {
    // Set the stdout console
    auto stdout_console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_console_sink->set_pattern(STDOUT_PATTERN);

    // Set stdout sinks
    std::vector<spdlog::sink_ptr> stdout_sinks;
    stdout_sinks.push_back(stdout_console_sink);

    // Set a log file with its full name, maximum size and the number of rotated files
    std::string log_fullname;
    if (!no_log_file) {
        log_fullname = fs::path(getenv("HOME")).string() + "/.carta/log/carta.log";
        auto stdout_log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_fullname, LOG_FILE_SIZE, ROTATED_LOG_FILES);
        stdout_log_file_sink->set_pattern(STDOUT_PATTERN);
        stdout_sinks.push_back(stdout_log_file_sink);
    }

    // Create the stdout logger
    auto stdout_logger = std::make_shared<spdlog::logger>(STDOUT_TAG, std::begin(stdout_sinks), std::end(stdout_sinks));

    // Set logger's level according to the verbosity number
    switch (verbosity) {
        case 0:
            stdout_logger->set_level(spdlog::level::off);
            break;
        case 1:
            stdout_logger->set_level(spdlog::level::critical);
            break;
        case 2:
            stdout_logger->set_level(spdlog::level::err);
            break;
        case 3:
            stdout_logger->set_level(spdlog::level::warn);
            break;
        case 5:
            stdout_logger->set_level(spdlog::level::debug);
            break;
        case 6:
            stdout_logger->set_level(spdlog::level::trace);
            break;
        default: {
            stdout_logger->set_level(spdlog::level::info);
            break;
        }
    }

    // Set flush policy on severity
    stdout_logger->flush_on(spdlog::level::err);

    // Register the stdout logger
    if (!spdlog::get(STDOUT_TAG)) {
        spdlog::register_logger(stdout_logger);
    } else {
        spdlog::critical("Duplicate registration of the logger: {}!", STDOUT_TAG);
    }

    // Set the default logger
    spdlog::set_default_logger(stdout_logger);

    if (!no_log_file) {
        spdlog::info("Writing to the log file: {}", log_fullname);
    }
}

using CET = CARTA::EventType;

std::unordered_map<CET, std::string> event_type_map = {{CET::EMPTY_EVENT, "EMPTY_EVENT"}, {CET::REGISTER_VIEWER, "REGISTER_VIEWER"},
    {CET::FILE_LIST_REQUEST, "FILE_LIST_REQUEST"}, {CET::FILE_INFO_REQUEST, "FILE_INFO_REQUEST"}, {CET::OPEN_FILE, "OPEN_FILE"},
    {CET::SET_IMAGE_CHANNELS, "SET_IMAGE_CHANNELS"}, {CET::SET_CURSOR, "SET_CURSOR"},
    {CET::SET_SPATIAL_REQUIREMENTS, "SET_SPATIAL_REQUIREMENTS"}, {CET::SET_HISTOGRAM_REQUIREMENTS, "SET_HISTOGRAM_REQUIREMENTS"},
    {CET::SET_STATS_REQUIREMENTS, "SET_STATS_REQUIREMENTS"}, {CET::SET_REGION, "SET_REGION"}, {CET::REMOVE_REGION, "REMOVE_REGION"},
    {CET::CLOSE_FILE, "CLOSE_FILE"}, {CET::SET_SPECTRAL_REQUIREMENTS, "SET_SPECTRAL_REQUIREMENTS"},
    {CET::START_ANIMATION, "START_ANIMATION"}, {CET::START_ANIMATION_ACK, "START_ANIMATION_ACK"}, {CET::STOP_ANIMATION, "STOP_ANIMATION"},
    {CET::REGISTER_VIEWER_ACK, "REGISTER_VIEWER_ACK"}, {CET::FILE_LIST_RESPONSE, "FILE_LIST_RESPONSE"},
    {CET::FILE_INFO_RESPONSE, "FILE_INFO_RESPONSE"}, {CET::OPEN_FILE_ACK, "OPEN_FILE_ACK"}, {CET::SET_REGION_ACK, "SET_REGION_ACK"},
    {CET::REGION_HISTOGRAM_DATA, "REGION_HISTOGRAM_DATA"}, {CET::SPATIAL_PROFILE_DATA, "SPATIAL_PROFILE_DATA"},
    {CET::SPECTRAL_PROFILE_DATA, "SPECTRAL_PROFILE_DATA"}, {CET::REGION_STATS_DATA, "REGION_STATS_DATA"}, {CET::ERROR_DATA, "ERROR_DATA"},
    {CET::ANIMATION_FLOW_CONTROL, "ANIMATION_FLOW_CONTROL"}, {CET::ADD_REQUIRED_TILES, "ADD_REQUIRED_TILES"},
    {CET::REMOVE_REQUIRED_TILES, "REMOVE_REQUIRED_TILES"}, {CET::RASTER_TILE_DATA, "RASTER_TILE_DATA"},
    {CET::REGION_LIST_REQUEST, "REGION_LIST_REQUEST"}, {CET::REGION_LIST_RESPONSE, "REGION_LIST_RESPONSE"},
    {CET::REGION_FILE_INFO_REQUEST, "REGION_FILE_INFO_REQUEST"}, {CET::REGION_FILE_INFO_RESPONSE, "REGION_FILE_INFO_RESPONSE"},
    {CET::IMPORT_REGION, "IMPORT_REGION"}, {CET::IMPORT_REGION_ACK, "IMPORT_REGION_ACK"}, {CET::EXPORT_REGION, "EXPORT_REGION"},
    {CET::EXPORT_REGION_ACK, "EXPORT_REGION_ACK"}, {CET::SET_CONTOUR_PARAMETERS, "SET_CONTOUR_PARAMETERS"},
    {CET::CONTOUR_IMAGE_DATA, "CONTOUR_IMAGE_DATA"}, {CET::RESUME_SESSION, "RESUME_SESSION"},
    {CET::RESUME_SESSION_ACK, "RESUME_SESSION_ACK"}, {CET::RASTER_TILE_SYNC, "RASTER_TILE_SYNC"},
    {CET::CATALOG_LIST_REQUEST, "CATALOG_LIST_REQUEST"}, {CET::CATALOG_LIST_RESPONSE, "CATALOG_LIST_RESPONSE"},
    {CET::CATALOG_FILE_INFO_REQUEST, "CATALOG_FILE_INFO_REQUEST"}, {CET::CATALOG_FILE_INFO_RESPONSE, "CATALOG_FILE_INFO_RESPONSE"},
    {CET::OPEN_CATALOG_FILE, "OPEN_CATALOG_FILE"}, {CET::OPEN_CATALOG_FILE_ACK, "OPEN_CATALOG_FILE_ACK"},
    {CET::CLOSE_CATALOG_FILE, "CLOSE_CATALOG_FILE"}, {CET::CATALOG_FILTER_REQUEST, "CATALOG_FILTER_REQUEST"},
    {CET::CATALOG_FILTER_RESPONSE, "CATALOG_FILTER_RESPONSE"}, {CET::SCRIPTING_REQUEST, "SCRIPTING_REQUEST"},
    {CET::SCRIPTING_RESPONSE, "SCRIPTING_RESPONSE"}, {CET::MOMENT_REQUEST, "MOMENT_REQUEST"}, {CET::MOMENT_RESPONSE, "MOMENT_RESPONSE"},
    {CET::MOMENT_PROGRESS, "MOMENT_PROGRESS"}, {CET::STOP_MOMENT_CALC, "STOP_MOMENT_CALC"}, {CET::SAVE_FILE, "SAVE_FILE"},
    {CET::SAVE_FILE_ACK, "SAVE_FILE_ACK"}, {CET::SPECTRAL_LINE_REQUEST, "SPECTRAL_LINE_REQUEST"},
    {CET::SPECTRAL_LINE_RESPONSE, "SPECTRAL_LINE_RESPONSE"}};

void LogReceivedEventType(const CARTA::EventType& event_type) {
    if (event_type_map.count(event_type)) {
        spdlog::trace("[<==] {}", event_type_map[event_type]);
    } else {
        spdlog::error("Unknown event type: {}!", event_type);
    }
}

void LogSentEventType(const CARTA::EventType& event_type) {
    if (event_type_map.count(event_type)) {
        spdlog::trace("[==>] {}", event_type_map[event_type]);
    } else {
        spdlog::error("Unknown event type: {}!", event_type);
    }
}