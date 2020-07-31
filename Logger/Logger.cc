#include "Logger.h"

#include <casacore/casa/OS/Directory.h>

#include <filesystem>

namespace fs = std::filesystem;

void CreateLoggers(std::string log_filename) {
    // Set a log file name and its path
    if (!log_filename.empty()) {
        try {
            casacore::File tmp_filename(log_filename);
            casacore::Directory tmp_dir = tmp_filename.path().dirName();
            if (!tmp_dir.exists() || !tmp_dir.isWritable() || fs::path(log_filename).filename().empty() ||
                !fs::is_regular_file(fs::status(log_filename))) {
                log_filename = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";
                spdlog::warn("Can not create a log file! Use the default path name {0}", log_filename);
            } else {
                spdlog::info("Set the log file {0}", log_filename);
            }
        } catch (...) {
            log_filename = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";
            spdlog::warn("Can not create a log file! Use the default path name {0}", log_filename);
        }
    } else {
        log_filename = fs::path(getenv("HOME")).string() + "/CARTA/log/carta.log";
        spdlog::info("Set the log file {0}", log_filename);
    }

    // Set a log file with its maximum size 5 MB and with 0 rotated file
    auto log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(log_filename, 1048576 * 5, 0);

    // Set a log file pattern
    log_file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] %v");

    // Create a logger (tag RECEIVE) and save its messages to a log file
    auto receive_logger = std::make_shared<spdlog::logger>("<<==", log_file_sink);
    receive_logger->info("\">>>>>>>>> Start the RECEIVE logger <<<<<<<<<\"");

    // Register the logger (tag RECEIVE) so we can get it globally
    spdlog::register_logger(receive_logger);

    // Create a logger (tag SEND) and save its messages to a log file
    auto send_logger = std::make_shared<spdlog::logger>("==>>", log_file_sink);
    send_logger->info("\">>>>>>>>> Start the SEND logger <<<<<<<<<\"");

    // Register the logger (tag SEND) so we can get it globally
    spdlog::register_logger(send_logger);
}

void LogReceiveEventType(uint16_t event_type) {
    std::string logger_name = "<<==";
    std::shared_ptr<spdlog::logger> receive_logger = spdlog::get(logger_name);
    if (!receive_logger) {
        spdlog::error("Fail to get the logger {0}", logger_name);
        return;
    }

    switch (event_type) {
        case CARTA::EventType::REGISTER_VIEWER: {
            receive_logger->info("REGISTER_VIEWER");
            break;
        }
        case CARTA::EventType::RESUME_SESSION: {
            receive_logger->info("RESUME_SESSION");
            break;
        }
        case CARTA::EventType::SET_IMAGE_CHANNELS: {
            receive_logger->info("SET_IMAGE_CHANNELS");
            break;
        }
        case CARTA::EventType::SET_CURSOR: {
            receive_logger->info("SET_CURSOR");
            break;
        }
        case CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS: {
            receive_logger->info("SET_HISTOGRAM_REQUIREMENTS");
            break;
        }
        case CARTA::EventType::CLOSE_FILE: {
            receive_logger->info("CLOSE_FILE");
            break;
        }
        case CARTA::EventType::START_ANIMATION: {
            receive_logger->info("START_ANIMATION");
            break;
        }
        case CARTA::EventType::STOP_ANIMATION: {
            receive_logger->info("STOP_ANIMATION");
            break;
        }
        case CARTA::EventType::ANIMATION_FLOW_CONTROL: {
            receive_logger->info("ANIMATION_FLOW_CONTROL");
            break;
        }
        case CARTA::EventType::FILE_INFO_REQUEST: {
            receive_logger->info("FILE_INFO_REQUEST");
            break;
        }
        case CARTA::EventType::FILE_LIST_REQUEST: {
            receive_logger->info("FILE_LIST_REQUEST");
            break;
        }
        case CARTA::EventType::OPEN_FILE: {
            receive_logger->info("OPEN_FILE");
            break;
        }
        case CARTA::EventType::ADD_REQUIRED_TILES: {
            receive_logger->info("ADD_REQUIRED_TILES");
            break;
        }
        case CARTA::EventType::REGION_LIST_REQUEST: {
            receive_logger->info("REGION_LIST_REQUEST");
            break;
        }
        case CARTA::EventType::REGION_FILE_INFO_REQUEST: {
            receive_logger->info("REGION_FILE_INFO_REQUEST");
            break;
        }
        case CARTA::EventType::IMPORT_REGION: {
            receive_logger->info("IMPORT_REGION");
            break;
        }
        case CARTA::EventType::EXPORT_REGION: {
            receive_logger->info("EXPORT_REGION");
            break;
        }
        case CARTA::EventType::SET_USER_PREFERENCES: {
            receive_logger->info("SET_USER_PREFERENCES");
            break;
        }
        case CARTA::EventType::SET_USER_LAYOUT: {
            receive_logger->info("SET_USER_LAYOUT");
            break;
        }
        case CARTA::EventType::SET_CONTOUR_PARAMETERS: {
            receive_logger->info("SET_CONTOUR_PARAMETERS");
            break;
        }
        case CARTA::EventType::SCRIPTING_RESPONSE: {
            receive_logger->info("SCRIPTING_RESPONSE");
            break;
        }
        case CARTA::EventType::SET_REGION: {
            receive_logger->info("SET_REGION");
            break;
        }
        case CARTA::EventType::REMOVE_REGION: {
            receive_logger->info("REMOVE_REGION");
            break;
        }
        case CARTA::EventType::SET_SPECTRAL_REQUIREMENTS: {
            receive_logger->info("SET_SPECTRAL_REQUIREMENTS");
            break;
        }
        case CARTA::EventType::CATALOG_LIST_REQUEST: {
            receive_logger->info("CATALOG_LIST_REQUEST");
            break;
        }
        case CARTA::EventType::CATALOG_FILE_INFO_REQUEST: {
            receive_logger->info("CATALOG_FILE_INFO_REQUEST");
            break;
        }
        case CARTA::EventType::OPEN_CATALOG_FILE: {
            receive_logger->info("OPEN_CATALOG_FILE");
            break;
        }
        case CARTA::EventType::CLOSE_CATALOG_FILE: {
            receive_logger->info("CLOSE_CATALOG_FILE");
            break;
        }
        case CARTA::EventType::CATALOG_FILTER_REQUEST: {
            receive_logger->info("CATALOG_FILTER_REQUEST");
            break;
        }
        case CARTA::EventType::SPECTRAL_LINE_REQUEST: {
            receive_logger->info("SPECTRAL_LINE_REQUEST");
            break;
        }
        case CARTA::EventType::SET_SPATIAL_REQUIREMENTS: {
            receive_logger->info("SET_SPATIAL_REQUIREMENTS");
            break;
        }
        case CARTA::EventType::SET_STATS_REQUIREMENTS: {
            receive_logger->info("SET_STATS_REQUIREMENTS");
            break;
        }
        default: {
            receive_logger->info("Unknown event type: {}!", event_type);
            break;
        }
    }
}

void LogSendEventType(CARTA::EventType event_type) {
    std::string logger_name = "==>>";
    std::shared_ptr<spdlog::logger> send_logger = spdlog::get(logger_name);
    if (!send_logger) {
        spdlog::error("Fail to get the logger {0}", logger_name);
        return;
    }

    switch (event_type) {
        case CARTA::EventType::EMPTY_EVENT: {
            send_logger->info("EMPTY_EVENT");
            break;
        }
        case CARTA::EventType::FILE_INFO_REQUEST: {
            send_logger->info("FILE_INFO_REQUEST");
            break;
        }
        case CARTA::EventType::START_ANIMATION_ACK: {
            send_logger->info("START_ANIMATION_ACK");
            break;
        }
        case CARTA::EventType::REGISTER_VIEWER_ACK: {
            send_logger->info("REGISTER_VIEWER_ACK");
            break;
        }
        case CARTA::EventType::FILE_LIST_RESPONSE: {
            send_logger->info("FILE_LIST_RESPONSE");
            break;
        }
        case CARTA::EventType::FILE_INFO_RESPONSE: {
            send_logger->info("FILE_INFO_RESPONSE");
            break;
        }
        case CARTA::EventType::OPEN_FILE_ACK: {
            send_logger->info("OPEN_FILE_ACK");
            break;
        }
        case CARTA::EventType::SET_REGION_ACK: {
            send_logger->info("SET_REGION_ACK");
            break;
        }
        case CARTA::EventType::REGION_HISTOGRAM_DATA: {
            send_logger->info("REGION_HISTOGRAM_DATA");
            break;
        }
        case CARTA::EventType::SPATIAL_PROFILE_DATA: {
            send_logger->info("SPATIAL_PROFILE_DATA");
            break;
        }
        case CARTA::EventType::SPECTRAL_PROFILE_DATA: {
            send_logger->info("SPECTRAL_PROFILE_DATA");
            break;
        }
        case CARTA::EventType::REGION_STATS_DATA: {
            send_logger->info("REGION_STATS_DATA");
            break;
        }
        case CARTA::EventType::ERROR_DATA: {
            send_logger->info("ERROR_DATA");
            break;
        }
        case CARTA::EventType::REMOVE_REQUIRED_TILES: {
            send_logger->info("REMOVE_REQUIRED_TILES");
            break;
        }
        case CARTA::EventType::RASTER_TILE_DATA: {
            send_logger->info("RASTER_TILE_DATA");
            break;
        }
        case CARTA::EventType::REGION_LIST_RESPONSE: {
            send_logger->info("REGION_LIST_RESPONSE");
            break;
        }
        case CARTA::EventType::REGION_FILE_INFO_RESPONSE: {
            send_logger->info("REGION_FILE_INFO_RESPONSE");
            break;
        }
        case CARTA::EventType::IMPORT_REGION_ACK: {
            send_logger->info("IMPORT_REGION_ACK");
            break;
        }
        case CARTA::EventType::EXPORT_REGION_ACK: {
            send_logger->info("EXPORT_REGION_ACK");
            break;
        }
        case CARTA::EventType::SET_USER_PREFERENCES_ACK: {
            send_logger->info("SET_USER_PREFERENCES_ACK");
            break;
        }
        case CARTA::EventType::SET_USER_LAYOUT_ACK: {
            send_logger->info("SET_USER_LAYOUT_ACK");
            break;
        }
        case CARTA::EventType::CONTOUR_IMAGE_DATA: {
            send_logger->info("CONTOUR_IMAGE_DATA");
            break;
        }
        case CARTA::EventType::RESUME_SESSION_ACK: {
            send_logger->info("RESUME_SESSION_ACK");
            break;
        }
        case CARTA::EventType::RASTER_TILE_SYNC: {
            send_logger->info("RASTER_TILE_SYNC");
            break;
        }
        case CARTA::EventType::CATALOG_LIST_RESPONSE: {
            send_logger->info("CATALOG_LIST_RESPONSE");
            break;
        }
        case CARTA::EventType::CATALOG_FILE_INFO_RESPONSE: {
            send_logger->info("CATALOG_FILE_INFO_RESPONSE");
            break;
        }
        case CARTA::EventType::OPEN_CATALOG_FILE_ACK: {
            send_logger->info("OPEN_CATALOG_FILE_ACK");
            break;
        }
        case CARTA::EventType::CATALOG_FILTER_RESPONSE: {
            send_logger->info("CATALOG_FILTER_RESPONSE");
            break;
        }
        case CARTA::EventType::SCRIPTING_REQUEST: {
            send_logger->info("SCRIPTING_REQUEST");
            break;
        }
        case CARTA::EventType::SPECTRAL_LINE_RESPONSE: {
            send_logger->info("SPECTRAL_LINE_RESPONSE");
            break;
        }
        default: {
            send_logger->info("Unknown event type: {}!", event_type);
            break;
        }
    }
}
