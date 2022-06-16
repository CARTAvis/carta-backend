/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018-2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "ProgramSettings.h"

#include <fstream>
#include <iostream>

#include <spdlog/fmt/fmt.h>
#include <cxxopts/cxxopts.hpp>

#include <casacore/images/Images/ImageOpener.h>

#include "Util/App.h"

using json = nlohmann::json;

namespace carta {

template <class T>
void applyOptionalArgument(T& val, const string& argument_name, const cxxopts::ParseResult& results) {
    if (results.count(argument_name)) {
        val = results[argument_name].as<T>();
    }
}

ProgramSettings::ProgramSettings(int argc, char** argv) {
    if (argc > 1) {
        debug_msgs.push_back("Using command-line settings");
    }
    ApplyCommandLineSettings(argc, argv);
    ApplyJSONSettings();
    // Push files after all settings are applied
    PushFilePaths();

    // Apply deprecated no_http flag
    if (no_http) {
        no_frontend = true;
        no_database = true;
    }
}

json ProgramSettings::JSONSettingsFromFile(const std::string& json_file_path) {
    std::ifstream ifs(json_file_path, std::ifstream::in);
    json j;

    if (!ifs.good()) {
        warning_msgs.push_back(fmt::format("Error reading config file {}.", json_file_path));
        return j;
    }

    try {
        j = json::parse(ifs, nullptr, true, true);
    } catch (json::exception& err) {
        warning_msgs.push_back(fmt::format("Error parsing config file {}.", json_file_path));
        warning_msgs.push_back(err.what());
    }

    for (const auto& [name, msg] : deprecated_options) {
        if (j.contains(name)) {
            AddDeprecationWarning(name, json_file_path);
        }
    }

    for (const auto& [key, elem] : int_keys_map) {
        if (j.contains(key) && !j[key].is_number_integer()) {
            auto msg = fmt::format(
                "Problem in config file {} at key {}: current value is {}, but a number is expected.", json_file_path, key, j[key].dump());
            warning_msgs.push_back(msg);
            j.erase(key);
        }
    }
    for (const auto& [key, elem] : bool_keys_map) {
        if (j.contains(key) && !j[key].is_boolean()) {
            auto msg = fmt::format(
                "Problem in config file {} at key {}: current value is {}, but a boolean is expected.", json_file_path, key, j[key].dump());
            warning_msgs.push_back(msg);
            j.erase(key);
        }
    }
    for (const auto& [key, elem] : strings_keys_map) {
        if (j.contains(key) && !j[key].is_string()) {
            auto msg = fmt::format(
                "Problem in config file {} at key {}: current value is {}, but a string is expected.", json_file_path, key, j[key].dump());
            warning_msgs.push_back(msg);
            j.erase(key);
        }
    }

    auto msg_and_remove = [&](const std::string& key) {
        auto msg =
            fmt::format("Problem in config file {} at key {}: current value is {}, but a number or a list of two numbers is expected.",
                json_file_path, key, j[key].dump());
        warning_msgs.push_back(msg);
        j.erase(key);
    };
    for (const auto& [key, elem] : vector_int_keys_map) {
        if (j.contains(key)) {
            if (j[key].size() == 1 && j[key].is_number()) {
                continue;
            } else if (j[key].is_array()) {
                if (j[key].size() > 2) {
                    msg_and_remove(key);
                } else if (j[key].size() == 2) {
                    if (!j[key][0].is_number() || !j[key][1].is_number()) {
                        msg_and_remove(key);
                    }
                } else if (j[key].size() == 1) {
                    if (!j[key].at(0).is_number()) {
                        msg_and_remove(key);
                    }
                } else {
                    // looks like things went well
                    continue;
                }
            } else {
                msg_and_remove(key);
            }
        }
    }

    return j;
}

void ProgramSettings::SetSettingsFromJSON(const json& j) {
    for (const auto& [key, elem] : int_keys_map) {
        if (!j.contains(key)) {
            continue;
        }
        *elem = j[key];
    }

    for (const auto& [key, elem] : bool_keys_map) {
        if (!j.contains(key)) {
            continue;
        }
        *elem = j[key];
    }

    for (const auto& [key, elem] : strings_keys_map) {
        if (!j.contains(key)) {
            continue;
        }
        *elem = j[key];
    }

    for (const auto& [key, elem] : vector_int_keys_map) {
        if (!j.contains(key)) {
            continue;
        }
        elem->resize(j[key].size());
        if (j[key].is_array()) {
            for (int i = 0; i < j[key].size(); ++i) {
                elem->at(i) = j[key][i];
            }
        } else {
            elem->at(0) = j[key];
        }
    }
}

void ProgramSettings::ApplyCommandLineSettings(int argc, char** argv) {
    std::vector<string> positional_arguments;

    cxxopts::Options options("carta", "Cube Analysis and Rendering Tool for Astronomy");
    // clang-format off
    // clang-format doesn't like this chain of calls
    options.add_options()
        ("h,help", "print usage")
        ("v,version", "print version")
        ("verbosity", "display verbose logging from this level",
         cxxopts::value<int>()->default_value(std::to_string(verbosity)), "<level>")
        ("no_log", "do not log output to a log file", cxxopts::value<bool>())
        ("log_performance", "enable performance debug logs", cxxopts::value<bool>())
        ("log_protocol_messages", "enable protocol message debug logs", cxxopts::value<bool>())
        ("no_frontend", "disable built-in HTTP frontend interface", cxxopts::value<bool>())
        ("no_database", "disable built-in HTTP database interface", cxxopts::value<bool>())
        ("no_browser", "don't open the frontend URL in a browser on startup", cxxopts::value<bool>())
        ("browser", "custom browser command", cxxopts::value<string>(), "<browser>")
        ("host", "only listen on the specified interface (IP address or hostname)", cxxopts::value<string>(), "<interface>")
        ("p,port", fmt::format("manually set the HTTP and WebSocket port (default: {} or nearest available port)", DEFAULT_SOCKET_PORT), cxxopts::value<std::vector<int>>(), "<port>")
        ("t,omp_threads", "manually set OpenMP thread pool count", cxxopts::value<int>(), "<threads>")
        ("top_level_folder", "set top-level folder for data files", cxxopts::value<string>(), "<dir>")
        ("frontend_folder", "set folder from which frontend files are served", cxxopts::value<string>(), "<dir>")
        ("exit_timeout", "number of seconds to stay alive after last session exits", cxxopts::value<int>(), "<sec>")
        ("initial_timeout", "number of seconds to stay alive at start if no clients connect", cxxopts::value<int>(), "<sec>")
        ("idle_timeout", "number of seconds to keep idle sessions alive", cxxopts::value<int>(), "<sec>")
        ("read_only_mode", "disable write requests", cxxopts::value<bool>())
        ("enable_scripting", "enable HTTP scripting interface", cxxopts::value<bool>())
        ("files", "files to load", cxxopts::value<std::vector<string>>(positional_arguments))
        ("no_user_config", "ignore user configuration file", cxxopts::value<bool>())
        ("no_system_config", "ignore system configuration file", cxxopts::value<bool>());

    options.add_options("Deprecated and debug")
        ("debug_no_auth", "accept all incoming WebSocket connections on the specified port(s) (not secure; use with caution!)", cxxopts::value<bool>())
        ("threads", "[deprecated] manually set number of event processing threads (no longer supported)", cxxopts::value<int>(), "<threads>")
        ("base", "[deprecated] set starting folder for data files (use the positional parameter instead)", cxxopts::value<string>(), "<dir>")
        ("root", "[deprecated] use 'top_level_folder' instead", cxxopts::value<string>(), "<dir>")
        ("no_http", "[deprecated] disable built-in HTTP frontend and database interfaces (use 'no_frontend' and/or 'no_database' instead)", cxxopts::value<bool>());
    // clang-format on

    options.positional_help("<file or folder to open>");
    options.parse_positional("files");
    auto result = options.parse(argc, argv);

    std::string log_levels(R"(
 0   off
 1   critical
 2   error
 3   warning
 4   info
 5   debug)");

    std::string extra = fmt::format(R"(
By default the CARTA backend uses the current directory as the starting data 
folder, and uses the root of the filesystem (/) as the top-level data folder. If 
a custom top-level folder is set with 'top_level_folder', the backend will be 
restricted from accessing files outside this directory. Positional parameters 
may be used to set a different starting directory or to open files on startup.

A built-in HTTP server is enabled by default. It serves the CARTA frontend and 
provides an interface to the CARTA database. These features can be disabled with
'no_frontend' and 'no_database', for example if the CARTA backend is being 
invoked by the CARTA controller, which manages access to the frontend and 
database independently. The HTTP server also provides a scripting interface, but
this must be enabled explicitly with 'enable_scripting'.

Frontend files are served from '{}' (relative to the location of the backend 
executable). A custom frontend location may be specified with 'frontend_folder'. 
By default the backend listens for HTTP and WebSocket connections on all 
available interfaces, and automatically selects the first available port 
starting from {}. 'host' may be used to restrict the backend to a specific 
interface. 'port' may be used to set a specific port or to provide a range of 
allowed ports.

On startup the backend prints out a URL which can be used to launch the 
frontend, and tries to open this URL in the default browser. It's possible to 
disable this attempt completely with 'no_browser', or to provide a custom 
browser command with 'browser'. 'no_browser' takes precedence. The custom 
browser command may contain the placeholder CARTA_URL, which will be replaced by 
the frontend URL. If the placeholder is omitted, the URL will be appended to the 
end.

By default the number of OpenMP threads is automatically set to the detected 
number of logical cores. A fixed number may be set with 'omp_threads'.

Logs are written both to the terminal and to a log file, '{}/log/carta.log' 
in the user's home directory. Logging to the file can be disabled with 'no_log'. 
The log level is set with 'verbosity'. Possible log levels are:{}

Performance and protocol message logging is disabled by default, but can be 
enabled with 'log_performance' and 'log_protocol_messages'. 'verbosity' takes 
precedence: the additional log messages will only be visible if the level is set
to 5 (debug). Performance logs are written to a separate log file, 
'{}/log/performance.log'.

The 'exit_timeout' and 'initial_timeout' options are provided to shut the 
backend down automatically if it is idle (if no clients are connected). 
'idle_timeout' allows the backend to kill frontend sessions that are idle (no 
longer sending messages to the backend).
    
Enabling 'read_only_mode' prevents the backend from writing data (for example, 
saving regions or generated images).
    
'no_user_config' and 'no_system_config' may be used to ignore the user and 
global configuration files, respectively.
)",
        CARTA_DEFAULT_FRONTEND_FOLDER, DEFAULT_SOCKET_PORT, CARTA_USER_FOLDER_PREFIX, log_levels, CARTA_USER_FOLDER_PREFIX);

    for (const auto& [name, msg] : deprecated_options) {
        if (result.count(name)) {
            AddDeprecationWarning(name, "commandline parameters");
        }
    }

    if (result.count("version")) {
        std::cout << VERSION_ID << std::endl;
        version = true;
        return;
    } else if (result.count("help")) {
        std::cout << options.help() << extra;
        help = true;
        return;
    }

    verbosity = result["verbosity"].as<int>();
    no_log = result["no_log"].as<bool>();
    log_performance = result["log_performance"].as<bool>();
    log_protocol_messages = result["log_protocol_messages"].as<bool>();

    no_http = result["no_http"].as<bool>(); // deprecated
    no_database = result["no_database"].as<bool>();
    no_frontend = result["no_frontend"].as<bool>();
    debug_no_auth = result["debug_no_auth"].as<bool>();
    no_browser = result["no_browser"].as<bool>();
    read_only_mode = result["read_only_mode"].as<bool>();
    enable_scripting = result["enable_scripting"].as<bool>();

    no_user_config = result.count("no_user_config") != 0;
    no_system_config = result.count("no_system_config") != 0;

    applyOptionalArgument(top_level_folder, "root", result);
    // Override deprecated "root" argument
    applyOptionalArgument(top_level_folder, "top_level_folder", result);

    applyOptionalArgument(frontend_folder, "frontend_folder", result);
    applyOptionalArgument(host, "host", result);
    applyOptionalArgument(port, "port", result);

    applyOptionalArgument(omp_thread_count, "omp_threads", result);
    applyOptionalArgument(wait_time, "exit_timeout", result);
    applyOptionalArgument(init_wait_time, "initial_timeout", result);

    applyOptionalArgument(idle_session_wait_time, "idle_timeout", result);

    applyOptionalArgument(browser, "browser", result);

    // base will be overridden by the positional argument if it exists and is a folder
    applyOptionalArgument(starting_folder, "base", result);

    for (const auto& arg : positional_arguments) {
        fs::path p(arg);
        std::error_code error_code;
        if (fs::exists(p, error_code)) {
            if (fs::is_directory(p, error_code)) {
                auto image_type = casacore::ImageOpener::imageType(p.string());
                if (image_type == casacore::ImageOpener::AIPSPP || image_type == casacore::ImageOpener::MIRIAD ||
                    image_type == casacore::ImageOpener::IMAGECONCAT || image_type == casacore::ImageOpener::IMAGEEXPR ||
                    image_type == casacore::ImageOpener::COMPLISTIMAGE) {
                    file_paths.push_back(p);
                } else {
                    starting_folder = p.string();
                    // Exit loop after first folder has been found and remove all existing files
                    file_paths.clear();
                    break;
                }
            } else if (!fs::is_regular_file(p, error_code)) {
                // Ignore invalid files
                file_paths.clear();
                break;
            } else {
                file_paths.push_back(p);
            }
        } else {
            // Ignore invalid files
            file_paths.clear();
        }
    }

    // produce JSON for overridding system and user configuration;
    // Options here need to match all options available for system and user settings
    command_line_settings = json({}); // needs to have empty JSON at least in case of no command line options
    for (const auto& [key, elem] : int_keys_map) {
        if (result.count(key)) {
            command_line_settings[key] = result[key].as<int>();
        }
    }
    for (const auto& [key, elem] : bool_keys_map) {
        if (result.count(key)) {
            command_line_settings[key] = result[key].as<bool>();
        }
    }
    for (const auto& [key, elem] : strings_keys_map) {
        if (result.count(key)) {
            command_line_settings[key] = result[key].as<std::string>();
        }
    }
    for (const auto& [key, elem] : vector_int_keys_map) {
        if (result.count(key)) {
            command_line_settings[key] = result[key].as<std::vector<int>>();
        }
    }
}

void ProgramSettings::ApplyJSONSettings() {
    user_directory = fs::path(getenv("HOME")) / CARTA_USER_FOLDER_PREFIX;
    const fs::path user_settings_path = user_directory / "backend.json";
    const fs::path system_settings_path = "/etc/carta/backend.json";

    json settings;
    std::error_code error_code;

    if (!no_system_config && fs::exists(system_settings_path, error_code)) {
        settings = JSONSettingsFromFile(system_settings_path.string());
        system_settings_json_exists = true;
        debug_msgs.push_back(fmt::format("Reading system settings from {}.", system_settings_path.string()));
    }

    if (!no_user_config && fs::exists(user_settings_path, error_code)) {
        auto user_settings = JSONSettingsFromFile(user_settings_path.string());
        user_settings_json_exists = true;
        debug_msgs.push_back(fmt::format("Reading user settings from {}.", user_settings_path.string()));
        settings.merge_patch(user_settings); // user on top of system
    }

    if (system_settings_json_exists || user_settings_json_exists) {
        settings.merge_patch(command_line_settings); // force command-line on top of user and sytem
        SetSettingsFromJSON(settings);
    }
}

void ProgramSettings::PushFilePaths() {
    if (file_paths.size()) {
        // Calculate paths relative to top level folder
        auto top_level_path = fs::absolute(top_level_folder).lexically_normal();
        for (const auto& p : file_paths) {
            auto relative_path = fs::absolute(p).lexically_normal().lexically_relative(top_level_path);
            files.push_back(relative_path.string());
        }
    }
}

void ProgramSettings::AddDeprecationWarning(const std::string& option, std::string where) {
    auto message = deprecated_options.at(option);
    warning_msgs.push_back(fmt::format("Option {} found in {} is deprecated. {}", option, where, message));
}

bool ProgramSettings::operator!=(const ProgramSettings& rhs) const {
    return GetTuple() != rhs.GetTuple();
}

bool ProgramSettings::operator==(const ProgramSettings& rhs) const {
    return GetTuple() == rhs.GetTuple();
}

} // namespace carta
