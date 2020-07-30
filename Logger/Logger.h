#ifndef CARTA_BACKEND__LOGGER_H_
#define CARTA_BACKEND__LOGGER_H_

#include <carta-protobuf/enums.pb.h>
#include <fmt/format.h> // This header must be before spdlog headers
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>

namespace fs = std::filesystem;

// Create loggers
void CreateLoggers();

// Log event types for receiving from the frontend
void LogReceiveEventType(uint16_t event_type);

// Log event types for sending from the Session
void LogSendEventType(CARTA::EventType event_type);

#endif // CARTA_BACKEND__LOGGER_H_