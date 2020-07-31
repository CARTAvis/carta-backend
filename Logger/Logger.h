#ifndef CARTA_BACKEND__LOGGER_H_
#define CARTA_BACKEND__LOGGER_H_

#include <carta-protobuf/enums.pb.h>
#include <fmt/format.h> // This header must be before spdlog headers
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

// Create loggers
void CreateLoggers(const std::string& log_dir);

// Log event types for receiving from the frontend
void LogReceivedEventType(const CARTA::EventType& event_type);

// Log event types for sending from the Session
void LogSentEventType(const CARTA::EventType& event_type);

// Fill the event type map
void FillEventTypeMap();

#endif // CARTA_BACKEND__LOGGER_H_