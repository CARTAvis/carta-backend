/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018- Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProtocolLogger.h"
#include "Main/ProgramSettings.h"

#include <spdlog/spdlog.h>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <regex>
#include <stdexcept>
#include <unordered_map>

namespace carta {

std::unique_ptr<ProtocolLogger> ProtocolLogger::instance = nullptr;

class NullProtocolLogger : public ProtocolLogger {
public:
    void LogEvent(CARTA::EventType, const std::string&) override {}
};

class ActiveProtocolLogger : public ProtocolLogger {
public:
    ActiveProtocolLogger() {
        BuildRegistry();
    }

    void LogEvent(CARTA::EventType type, const std::string& direction) override {
        uint64_t current_count = 0;
        {
            std::lock_guard<std::mutex> lock(counter_mutex);
            current_count = ++event_count[type];
        }

        try {
            auto& action = registry.at(type);
            auto* descriptor = CARTA::EventType_descriptor();
            std::string name = descriptor->FindValueByNumber(type)->name();
            action(name, current_count, direction);
        } catch (const std::out_of_range&) {
            spdlog::warn("[protocol] unknown event type: {}", static_cast<int>(type));
        }
    }

private:
    using LogAction = std::function<void(const std::string& name, uint64_t& count, const std::string& dir)>;

    std::unordered_map<CARTA::EventType, LogAction> registry;
    std::unordered_map<CARTA::EventType, uint64_t> event_count;
    std::mutex counter_mutex;

    void BuildRegistry();

    LogAction createNoOpLogger() {
        return [](auto&, auto&, auto&) {};
    }
    LogAction createNormalLogger() {
        return [](const std::string& n, uint64_t&, const std::string& d) { spdlog::debug("[protocol] {} {}", d, n); };
    }
    LogAction createThrottledLogger(uint32_t first_n, uint32_t every_m) {
        return [first_n, every_m](const std::string& n, uint64_t& c, const std::string& d) {
            if (c <= first_n || c % every_m == 0) {
                std::string s = (c > first_n) ? fmt::format(" (Total: {})", c) : "";
                spdlog::debug("[protocol] {} {}{}", d, n, s);
            }
        };
    }
};

void ProtocolLogger::Init() {
    auto& settings = ProgramSettings::GetInstance();
    if (settings.log_protocol_messages) {
        instance = std::make_unique<ActiveProtocolLogger>();
    } else {
        instance = std::make_unique<NullProtocolLogger>();
    }
}

void ActiveProtocolLogger::BuildRegistry() {
    auto& settings = ProgramSettings::GetInstance();
    auto* descriptor = CARTA::EventType_descriptor();

    const auto& rules = settings.logging_rules;

    for (int i = 0; i < descriptor->value_count(); ++i) {
        auto* value = descriptor->value(i);
        auto event_type = static_cast<CARTA::EventType>(value->number());
        std::string name = value->name();

        bool matched = false;
        for (const auto& rule : rules) {
            std::regex pattern(rule.match);
            if (std::regex_match(name, pattern)) {
                if (rule.action == "throttle") {
                    registry[event_type] = createThrottledLogger(rule.first_n, rule.every_m);
                } else if (rule.action == "none") {
                    registry[event_type] = createNoOpLogger();
                } else {
                    registry[event_type] = createNormalLogger();
                }
                matched = true;
                break;
            }
        }

        if (!matched) {
            registry[event_type] = createNormalLogger();
        }
    }
}

std::vector<LoggingRule> ProtocolLogger::GetDefaultRules() {
    return {// High-frequency events: Throttle to prevent log flooding
        {".*TILE_DATA|SPATIAL_PROFILE_DATA", "throttle", 3, 500}, {"SET_CURSOR|SET_SPATIAL_REQUIREMENTS", "throttle", 5, 100},

        // Noisy events: Silence these by default
        {"PING", "none", -1, -1},

        // Catch-all: Log everything else normally
        {".*", "normal", -1, -1}};
}

} // namespace carta
