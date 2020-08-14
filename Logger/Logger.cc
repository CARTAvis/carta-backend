#include "Logger.h"

#include <filesystem>

namespace fs = std::filesystem;

static std::size_t log_file_size = 1024 * 1024 * 5; // (Bytes)
static std::size_t rotated_files = 0;
static std::string log_default_dir = fs::path(getenv("HOME")).string() + "/CARTA/log/";
static std::string icd_log_name("icd_msg.log");
static std::string outgoing_tag("==>");
static std::string incoming_tag("<==");

std::unordered_map<CARTA::EventType, std::string> event_type_map;

void CreateLoggers(const std::string& log_dir) {
    // Set ICD log file name and its path
    std::string icd_log_full_name;
    if (!log_dir.empty()) {
        try {
            icd_log_full_name = log_dir + "/" + icd_log_name;
            fs::path tmp_dir = fs::path(icd_log_full_name).parent_path();
            if (!fs::exists(tmp_dir) || access(tmp_dir.string().c_str(), W_OK)) {
                icd_log_full_name = log_default_dir + icd_log_name;
                spdlog::warn("Can not create a log file! Use the default path name {0}", icd_log_full_name);
            } else {
                spdlog::info("Set the log file {0}", icd_log_full_name);
            }
        } catch (...) {
            icd_log_full_name = log_default_dir + icd_log_name;
            spdlog::warn("Can not create a log file! Use the default path name {0}", icd_log_full_name);
        }
    } else {
        icd_log_full_name = log_default_dir + icd_log_name;
        spdlog::info("Set the log file {0}", icd_log_full_name);
    }

    // Set a log file with its maximum size and the number of rotated files
    auto icd_log_file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(icd_log_full_name, log_file_size, rotated_files);

    // Set a log file pattern
    icd_log_file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] %v");

    // Create a logger (incoming tag) and save its messages to a log file
    auto incoming_logger = std::make_shared<spdlog::logger>(incoming_tag, icd_log_file_sink);
    incoming_logger->info("\">>>>>>>>> Start the incoming logger <<<<<<<<<\"");

    // Register the logger (incoming tag) so we can get it globally
    spdlog::register_logger(incoming_logger);

    // Create a logger (outgoing tag) and save its messages to a log file
    auto outgoing_logger = std::make_shared<spdlog::logger>(outgoing_tag, icd_log_file_sink);
    outgoing_logger->info("\">>>>>>>>> Start the outgoing logger <<<<<<<<<\"");

    // Register the logger (outgoing tag) so we can get it globally
    spdlog::register_logger(outgoing_logger);

    // Fill the event type map
    FillEventTypeMap();
}

void LogReceivedEventType(const CARTA::EventType& event_type) {
    std::shared_ptr<spdlog::logger> incoming_logger = spdlog::get(incoming_tag);
    if (!incoming_logger) {
        spdlog::error("Fail to get the logger {0}", incoming_tag);
        return;
    }
    if (event_type_map.count(event_type)) {
        incoming_logger->info("{0}", event_type_map[event_type]);
    } else {
        incoming_logger->info("Unknown event type: {}!", event_type);
    }
}

void LogSentEventType(const CARTA::EventType& event_type) {
    std::shared_ptr<spdlog::logger> outgoing_logger = spdlog::get(outgoing_tag);
    if (!outgoing_logger) {
        spdlog::error("Fail to get the logger {}", outgoing_tag);
        return;
    }
    if (event_type_map.count(event_type)) {
        outgoing_logger->info("{0}", event_type_map[event_type]);
    } else {
        outgoing_logger->info("Unknown event type: {}!", event_type);
    }
}

inline void FillEventTypeMap() {
    event_type_map[CARTA::EventType::REGISTER_VIEWER] = "REGISTER_VIEWER";
    event_type_map[CARTA::EventType::RESUME_SESSION] = "RESUME_SESSION";
    event_type_map[CARTA::EventType::SET_IMAGE_CHANNELS] = "SET_IMAGE_CHANNELS";
    event_type_map[CARTA::EventType::SET_CURSOR] = "SET_CURSOR";
    event_type_map[CARTA::EventType::SET_HISTOGRAM_REQUIREMENTS] = "SET_HISTOGRAM_REQUIREMENTS";
    event_type_map[CARTA::EventType::CLOSE_FILE] = "CLOSE_FILE";
    event_type_map[CARTA::EventType::START_ANIMATION] = "START_ANIMATION";
    event_type_map[CARTA::EventType::STOP_ANIMATION] = "STOP_ANIMATION";
    event_type_map[CARTA::EventType::ANIMATION_FLOW_CONTROL] = "ANIMATION_FLOW_CONTROL";
    event_type_map[CARTA::EventType::FILE_INFO_REQUEST] = "FILE_INFO_REQUEST";
    event_type_map[CARTA::EventType::FILE_LIST_REQUEST] = "FILE_LIST_REQUEST";
    event_type_map[CARTA::EventType::OPEN_FILE] = "OPEN_FILE";
    event_type_map[CARTA::EventType::ADD_REQUIRED_TILES] = "ADD_REQUIRED_TILES";
    event_type_map[CARTA::EventType::REGION_LIST_REQUEST] = "REGION_LIST_REQUEST";
    event_type_map[CARTA::EventType::REGION_FILE_INFO_REQUEST] = "REGION_FILE_INFO_REQUEST";
    event_type_map[CARTA::EventType::IMPORT_REGION] = "IMPORT_REGION";
    event_type_map[CARTA::EventType::EXPORT_REGION] = "EXPORT_REGION";
    event_type_map[CARTA::EventType::SET_USER_PREFERENCES] = "SET_USER_PREFERENCES";
    event_type_map[CARTA::EventType::SET_USER_LAYOUT] = "SET_USER_LAYOUT";
    event_type_map[CARTA::EventType::SET_CONTOUR_PARAMETERS] = "SET_CONTOUR_PARAMETERS";
    event_type_map[CARTA::EventType::SCRIPTING_RESPONSE] = "SCRIPTING_RESPONSE";
    event_type_map[CARTA::EventType::SET_REGION] = "SET_REGION";
    event_type_map[CARTA::EventType::REMOVE_REGION] = "REMOVE_REGION";
    event_type_map[CARTA::EventType::SET_SPECTRAL_REQUIREMENTS] = "SET_SPECTRAL_REQUIREMENTS";
    event_type_map[CARTA::EventType::CATALOG_LIST_REQUEST] = "CATALOG_LIST_REQUEST";
    event_type_map[CARTA::EventType::CATALOG_FILE_INFO_REQUEST] = "CATALOG_FILE_INFO_REQUEST";
    event_type_map[CARTA::EventType::OPEN_CATALOG_FILE] = "OPEN_CATALOG_FILE";
    event_type_map[CARTA::EventType::CLOSE_CATALOG_FILE] = "CLOSE_CATALOG_FILE";
    event_type_map[CARTA::EventType::CATALOG_FILTER_REQUEST] = "CATALOG_FILTER_REQUEST";
    event_type_map[CARTA::EventType::SPECTRAL_LINE_REQUEST] = "SPECTRAL_LINE_REQUEST";
    event_type_map[CARTA::EventType::SET_SPATIAL_REQUIREMENTS] = "SET_SPATIAL_REQUIREMENTS";
    event_type_map[CARTA::EventType::SET_STATS_REQUIREMENTS] = "SET_STATS_REQUIREMENTS";
    event_type_map[CARTA::EventType::EMPTY_EVENT] = "EMPTY_EVENT";

    event_type_map[CARTA::EventType::FILE_INFO_RESPONSE] = "FILE_INFO_RESPONSE";
    event_type_map[CARTA::EventType::START_ANIMATION_ACK] = "START_ANIMATION_ACK";
    event_type_map[CARTA::EventType::REGISTER_VIEWER_ACK] = "REGISTER_VIEWER_ACK";
    event_type_map[CARTA::EventType::FILE_LIST_RESPONSE] = "FILE_LIST_RESPONSE";
    event_type_map[CARTA::EventType::OPEN_FILE_ACK] = "OPEN_FILE_ACK";
    event_type_map[CARTA::EventType::SET_REGION_ACK] = "SET_REGION_ACK";
    event_type_map[CARTA::EventType::REGION_HISTOGRAM_DATA] = "REGION_HISTOGRAM_DATA";
    event_type_map[CARTA::EventType::SPATIAL_PROFILE_DATA] = "SPATIAL_PROFILE_DATA";
    event_type_map[CARTA::EventType::SPECTRAL_PROFILE_DATA] = "SPECTRAL_PROFILE_DATA";
    event_type_map[CARTA::EventType::REGION_STATS_DATA] = "REGION_STATS_DATA";
    event_type_map[CARTA::EventType::ERROR_DATA] = "ERROR_DATA";
    event_type_map[CARTA::EventType::REMOVE_REQUIRED_TILES] = "REMOVE_REQUIRED_TILES";
    event_type_map[CARTA::EventType::RASTER_TILE_DATA] = "RASTER_TILE_DATA";
    event_type_map[CARTA::EventType::REGION_LIST_RESPONSE] = "REGION_LIST_RESPONSE";
    event_type_map[CARTA::EventType::REGION_FILE_INFO_RESPONSE] = "REGION_FILE_INFO_RESPONSE";
    event_type_map[CARTA::EventType::IMPORT_REGION_ACK] = "IMPORT_REGION_ACK";
    event_type_map[CARTA::EventType::EXPORT_REGION_ACK] = "EXPORT_REGION_ACK";
    event_type_map[CARTA::EventType::SET_USER_PREFERENCES_ACK] = "SET_USER_PREFERENCES_ACK";
    event_type_map[CARTA::EventType::SET_USER_LAYOUT_ACK] = "SET_USER_LAYOUT_ACK";
    event_type_map[CARTA::EventType::CONTOUR_IMAGE_DATA] = "CONTOUR_IMAGE_DATA";
    event_type_map[CARTA::EventType::RESUME_SESSION_ACK] = "RESUME_SESSION_ACK";
    event_type_map[CARTA::EventType::RASTER_TILE_SYNC] = "RASTER_TILE_SYNC";
    event_type_map[CARTA::EventType::CATALOG_LIST_RESPONSE] = "CATALOG_LIST_RESPONSE";
    event_type_map[CARTA::EventType::CATALOG_FILE_INFO_RESPONSE] = "CATALOG_FILE_INFO_RESPONSE";
    event_type_map[CARTA::EventType::OPEN_CATALOG_FILE_ACK] = "OPEN_CATALOG_FILE_ACK";
    event_type_map[CARTA::EventType::CATALOG_FILTER_RESPONSE] = "CATALOG_FILTER_RESPONSE";
    event_type_map[CARTA::EventType::SCRIPTING_REQUEST] = "SCRIPTING_REQUEST";
    event_type_map[CARTA::EventType::SPECTRAL_LINE_RESPONSE] = "SPECTRAL_LINE_RESPONSE";
}